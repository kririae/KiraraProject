#include "flux/FLux/TomlScene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/FLux/FLuxCLI.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Mtlx/MtlxShaderGenerator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
/// \brief The BSDF and the EDF that the shader of a material gives.
///
/// Both come from one shader instance. Whether the EDF may emit is the property of its shader.
struct Material {
    Ref<BSDF const> bsdf;
    Ref<EDF const> edf;
};

/// \brief Creates the \c Material of the \c file and \c material properties of \p props.
[[nodiscard]] Material createMaterial(
    Context &context, std::optional<MtlxShaderGenerator> &generator, kira::Properties &props
) {
    auto const type = props.use<std::string>("type");
    if (type != "materialx")
        throw kira::Anyhow("unsupported material type '{}'", type);

    auto const authoredPath = props.use<std::filesystem::path>("file");
    auto const materialName = props.use<std::string>("material");
    auto const path = context.getFileResolver().resolve(authoredPath);
    if (!std::filesystem::exists(path))
        throw kira::Anyhow("failed to resolve MaterialX file '{}'", authoredPath.string());

    if (!generator)
        generator.emplace(FLUX_MATERIALX_DIR);
    auto stream = std::ifstream{path};
    if (!stream)
        throw kira::Anyhow("failed to read MaterialX file '{}'", path.string());
    auto const document =
        std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

    auto generation = generator->generate(document, materialName, std::array{path.parent_path()});
    for (auto const &diagnostic : generation.diagnostics)
        LogWarn("{}", diagnostic);
    auto const &instance = generation.instance;
    if (!instance)
        throw kira::Anyhow(
            "MaterialX file '{}' generated no shader for material '{}'", path.string(), materialName
        );
    auto textures = std::vector<Ref<ImageTexture const>>{};
    for (auto const &spec : instance->textures)
        textures.push_back(context.create<ImageTexture>(spec));

    return {
        .bsdf = context.create<ShaderBSDF>(instance->spec, instance->words, textures),
        .edf = context.create<ShaderEDF>(instance->spec, instance->words, textures),
    };
}
} // namespace

LoadedScene loadTomlScene(FluxCLIOptions const &options) {
    auto const extension = options.scenePath.extension().string();
    if (extension != ".toml")
        throw kira::Anyhow("unsupported scene extension '{}'", extension);

    std::ifstream stream{options.scenePath, std::ios::binary};
    if (!stream)
        throw kira::Anyhow("failed to open scene '{}'", options.scenePath.string());
    std::string const source{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
    if (stream.bad())
        throw kira::Anyhow("failed to read scene '{}'", options.scenePath.string());

    auto scene = kira::Properties::parse(source, options.scenePath);
    auto context = Context::create();
    context->getFileResolver().prepend(std::filesystem::absolute(options.scenePath).parent_path());
    auto integratorProps = scene.use_view("integrator");
    if (options.chunkSize)
        integratorProps.set("chunk_size", *options.chunkSize);
    context->setActiveIntegrator(context->create<PathIntegrator>(integratorProps));
    context->setActiveSampler(context->create<Sampler>(
        scene.contains("sampler") ? scene.use_view("sampler") : kira::Properties{}
    ));
    if (scene.contains("envmap"))
        context->setActiveEnvMap(context->create<EnvMapLight>(scene.use_view("envmap")));

    auto camera = Camera::create(scene.use_view("camera"));
    auto filmProps = scene.use_view("film");
    auto const resolution = filmProps.use<Vec2u>("resolution");
    auto const samplesPerPixel =
        options.samplesPerPixel.value_or(filmProps.use_or<std::uint32_t>("num_samples", 1));

    std::unordered_map<std::string, Ref<BSDF const>> bsdfs;
    if (scene.contains("bsdf")) {
        auto bsdfProps = scene.use_array_view("bsdf");
        bsdfs.reserve(bsdfProps.size());
        for (std::size_t index = 0; index < bsdfProps.size(); ++index) {
            auto props = bsdfProps.get_view(index);
            auto name = props.use<std::string>("name");
            if (bsdfs.contains(name))
                throw kira::Anyhow("duplicate BSDF name '{}'", name);
            bsdfs.emplace(std::move(name), context->create<BSDF>(props));
        }
    }

    std::unordered_map<std::string, Material> materials;
    std::optional<MtlxShaderGenerator> mtlxGenerator;
    if (scene.contains("material")) {
        auto materialProps = scene.use_array_view("material");
        materials.reserve(materialProps.size());
        for (std::size_t index = 0; index < materialProps.size(); ++index) {
            auto props = materialProps.get_view(index);
            auto name = props.use<std::string>("name");
            if (materials.contains(name))
                throw kira::Anyhow("duplicate material name '{}'", name);
            materials.emplace(std::move(name), createMaterial(*context, mtlxGenerator, props));
        }
    }

    if (scene.contains("primitive")) {
        auto primitives = scene.use_array_view("primitive");
        for (std::size_t index = 0; index < primitives.size(); ++index) {
            auto props = primitives.get_view(index);
            if (props.is_type_of<std::string>("bsdf")) {
                auto const name = props.use<std::string>("bsdf");
                auto const iterator = bsdfs.find(name);
                if (iterator == bsdfs.end())
                    throw kira::Anyhow("primitive references unknown BSDF '{}'", name);
                props.set(
                    "bsdf_ctx_id", static_cast<std::int64_t>(iterator->second->getContextId())
                );
            }

            if (props.contains("material")) {
                if (props.contains("bsdf"))
                    throw kira::Anyhow("primitive has both a material and a BSDF");

                auto const name = props.use<std::string>("material");
                auto const iterator = materials.find(name);
                if (iterator == materials.end())
                    throw kira::Anyhow("primitive references unknown material '{}'", name);
                if (props.contains("light"))
                    throw kira::Anyhow("primitive has both a material and a light");
                auto const &material = iterator->second;
                props.set("bsdf_ctx_id", static_cast<std::int64_t>(material.bsdf->getContextId()));
                props.set("edf_ctx_id", static_cast<std::int64_t>(material.edf->getContextId()));
            }

            if (props.contains("light")) {
                auto lightProps = props.use_view("light");
                auto const type = lightProps.use_or<std::string>("type", "area");
                if (type != "area")
                    throw kira::Anyhow("primitive uses unsupported light type '{}'", type);
                kira::Properties edfProps;
                edfProps.set("type", std::string{"constant"});
                edfProps.set("radiance", lightProps.use<Spectrum>("emission"));
                props.set("edf", edfProps);
            }

            (void)context->create<Primitive>(props);
        }
    }

    return {
        .context = std::move(context),
        .camera = std::move(camera),
        .film = {.resolution = resolution, .channels = FilmChannels::Color},
        .samplesPerPixel = samplesPerPixel,
    };
}
} // namespace flux
