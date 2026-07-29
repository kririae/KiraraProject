#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

#include "flux/Embree/EmbreeHandler.h"
#include "flux/FLuxCLI.h"
#include "flux/IO/ImageIO.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "kira/Anyhow.h"

#ifndef FLUX_OPTIX_IR
#error "FLUX_OPTIX_IR must name the Flux OptiX IR module"
#endif

namespace {
struct RenderSetup {
    flux::Ref<flux::Context> context;
    flux::Ref<flux::RenderProduct> product;
};

[[nodiscard]] RenderSetup loadScene(flux::FluxCLIRequest const &request) {
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

    kira::Properties scene{
        toml::parse(source, request.scenePath.string()),
        source,
    };
    auto context = flux::Context::create();

    auto integratorProperties = scene.use_view("integrator");
    auto const integratorType = integratorProperties.use<std::string>("type");
    if (integratorType != "path")
        throw kira::Anyhow("unsupported integrator type '{}'", integratorType);
    (void)context->create<flux::PathIntegrator>();

    if (scene.contains("sampler")) {
        auto samplerProperties = scene.use_view("sampler");
        auto const samplerType = samplerProperties.use_or<std::string>("type", "independent");
        if (samplerType != "independent")
            throw kira::Anyhow("unsupported sampler type '{}'", samplerType);
    }
    (void)context->create<flux::IndependentSampler>();

    auto camera = flux::Camera::create(scene.use_view("camera"));
    auto filmProperties = scene.use_view("film");
    auto const resolution = filmProperties.use<flux::Vec2u>("resolution");
    auto const configuredSamples = filmProperties.use_or<std::uint32_t>("num_samples", 1);

    kira::Properties productProperties;
    productProperties.set("width", resolution.x());
    productProperties.set("height", resolution.y());
    productProperties.set("num_samples", request.samplesPerPixel.value_or(configuredSamples));
    auto product = flux::RenderProduct::create(camera, std::move(productProperties));
    product->getFilm().setChannels(flux::FilmChannels::Color);

    std::unordered_map<std::string, std::size_t> bsdfContextIds;
    if (scene.contains("bsdf")) {
        auto bsdfs = scene.use_array_view("bsdf");
        bsdfContextIds.reserve(bsdfs.size());
        for (std::size_t index = 0; index < bsdfs.size(); ++index) {
            auto properties = bsdfs.get_view(index);
            auto const type = properties.use<std::string>("type");
            if (type != "diffuse")
                throw kira::Anyhow("unsupported BSDF type '{}'", type);

            auto name = properties.use<std::string>("name");
            if (bsdfContextIds.contains(name))
                throw kira::Anyhow("duplicate BSDF name '{}'", name);
            auto bsdf = context->create<flux::DiffuseBSDF>(std::move(properties));
            bsdfContextIds.emplace(std::move(name), bsdf->getContextId());
        }
    }

    auto primitives = scene.use_array_view("primitive");
    for (std::size_t index = 0; index < primitives.size(); ++index) {
        auto properties = primitives.get_view(index);
        auto const type = properties.use<std::string>("type");
        if (type != "trimesh")
            throw kira::Anyhow("unsupported primitive type '{}'", type);

        auto meshPath = properties.use<std::filesystem::path>("path");
        if (meshPath.is_relative())
            meshPath = request.scenePath.parent_path() / meshPath;
        kira::Properties meshProperties;
        meshProperties.set("path", meshPath.lexically_normal());
        auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));

        kira::Properties primitiveProperties;
        primitiveProperties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
        if (properties.contains("bsdf")) {
            auto const bsdfName = properties.use<std::string>("bsdf");
            auto const iterator = bsdfContextIds.find(bsdfName);
            if (iterator == bsdfContextIds.end())
                throw kira::Anyhow("primitive references unknown BSDF '{}'", bsdfName);
            primitiveProperties.set("bsdf_ctx_id", static_cast<std::int64_t>(iterator->second));
        }
        (void)context->create<flux::Primitive>(std::move(primitiveProperties));
    }

    // This compatibility light approximates cbox's ceiling emitter.
    kira::Properties lightProperties;
    lightProperties.set("position", flux::Vec3f{0.0F, 1.8F, 0.0F});
    lightProperties.set("intensity", flux::Spectrum{4.25F, 3.0F, 1.25F});
    (void)context->create<flux::PointLight>(std::move(lightProperties));

    return {
        .context = std::move(context),
        .product = std::move(product),
    };
}
} // namespace

int main(int argc, char **argv) try {
    auto const request = flux::parseFluxCLI(argc, argv);
    auto setup = loadScene(request);

    if (request.backend == flux::RenderBackend::Optix) {
        flux::OptixHandler handler{setup.context, std::filesystem::path{FLUX_OPTIX_IR}};
        handler.render(*setup.product, setup.product->getSamplesPerPixel());
        handler.download(*setup.product);
    } else {
        flux::EmbreeHandler handler{setup.context};
        handler.render(*setup.product, setup.product->getSamplesPerPixel());
        handler.download(*setup.product);
    }

    auto outputPath = request.outputPath.value_or(request.scenePath);
    if (!request.outputPath)
        outputPath.replace_extension(".exr");
    flux::writeImage<flux::ColorChannel>(outputPath, setup.product->getFilm());
} catch (std::exception const &exception) {
    std::cerr << "flux: " << exception.what() << '\n';
    return 1;
}
