#include "flux/FLux/TomlScene.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

#include "flux/FLux/FLuxCLI.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Shading/BSDF.h"
#include "kira/Anyhow.h"

namespace flux {
LoadedScene loadTomlScene(FluxCLIRequest const &request) {
    auto const extension = request.scenePath.extension().string();
    if (extension != ".toml")
        throw kira::Anyhow("unsupported scene extension '{}'", extension);

    std::ifstream stream{request.scenePath, std::ios::binary};
    if (!stream)
        throw kira::Anyhow("failed to open scene '{}'", request.scenePath.string());
    std::string const source{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
    if (stream.bad())
        throw kira::Anyhow("failed to read scene '{}'", request.scenePath.string());

    auto scene = kira::Properties::parse(source, request.scenePath);
    auto context = Context::create();
    context->getFileResolver().prepend(std::filesystem::absolute(request.scenePath).parent_path());
    (void)context->create<PathIntegrator>(scene.use_view("integrator"));
    (void)context->create<Sampler>(
        scene.contains("sampler") ? scene.use_view("sampler") : kira::Properties{}
    );

    auto camera = Camera::create(scene.use_view("camera"));
    auto filmProps = scene.use_view("film");
    if (request.samplesPerPixel)
        filmProps.set("num_samples", *request.samplesPerPixel);
    auto product = RenderProduct::create(std::move(camera), filmProps);
    product->getFilm().setChannels(FilmChannels::Color);

    std::unordered_map<std::string, Ref<BSDF const>> bsdfs;
    if (scene.contains("bsdf")) {
        auto bsdfProps = scene.use_array_view("bsdf");
        bsdfs.reserve(bsdfProps.size());
        for (std::size_t index = 0; index < bsdfProps.size(); ++index) {
            auto props = bsdfProps.get_view(index);
            auto name = props.use<std::string>("name");
            if (bsdfs.contains(name))
                throw kira::Anyhow("duplicate BSDF name '{}'", name);
            auto bsdf = context->create<BSDF>(props);
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
        .product = std::move(product),
    };
}
} // namespace flux
