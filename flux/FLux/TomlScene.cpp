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
#include "flux/Mtlx/MtlxBSDFGenerator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
/// \brief Creates the \c ShaderBSDF of the material that the \c file and \c material properties
/// name.
[[nodiscard]] Ref<BSDF const> createMaterialXBSDF(
    Context &context, std::optional<MtlxBSDFGenerator> &generator, kira::Properties &props
) {
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
    auto &instance = generation.instance;
    if (!instance)
        throw kira::Anyhow(
            "MaterialX file '{}' generated nothing for material '{}'", path.string(), materialName
        );
    auto textures = std::vector<Ref<ImageTexture const>>{};
    for (auto const &spec : instance->textures)
        textures.push_back(context.create<ImageTexture>(spec));
    return context.create<ShaderBSDF>(
        instance->spec, std::move(instance->words), std::move(textures)
    );
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
    std::optional<MtlxBSDFGenerator> mtlxGenerator;
    if (scene.contains("bsdf")) {
        auto bsdfProps = scene.use_array_view("bsdf");
        bsdfs.reserve(bsdfProps.size());
        for (std::size_t index = 0; index < bsdfProps.size(); ++index) {
            auto props = bsdfProps.get_view(index);
            auto name = props.use<std::string>("name");
            if (bsdfs.contains(name))
                throw kira::Anyhow("duplicate BSDF name '{}'", name);
            auto bsdf = props.is_type_of<std::string>("type") &&
                                props.use<std::string>("type") == "materialx"
                            ? createMaterialXBSDF(*context, mtlxGenerator, props)
                            : context->create<BSDF>(props);
            bsdfs.emplace(std::move(name), std::move(bsdf));
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
