#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <utility>

#include "flux/Embree/EmbreeHandler.h"
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

#ifndef FLUX_OPTIX_IR
#error "FLUX_OPTIX_IR must name the Flux OptiX IR module"
#endif

int main(int argc, char **argv) try {
    if (argc != 3) {
        std::cerr << "Usage: flux <mesh.obj> <output-directory>\n";
        return 1;
    }

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path{argv[1]});
    auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));

    kira::Properties bsdfProperties;
    bsdfProperties.set("reflectance", flux::Spectrum{0.65F, 0.30F, 0.15F});
    auto bsdf = context->create<flux::DiffuseBSDF>(std::move(bsdfProperties));

    kira::Properties primitiveProperties;
    primitiveProperties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    primitiveProperties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    (void)context->create<flux::Primitive>(std::move(primitiveProperties));

    kira::Properties lightProperties;
    lightProperties.set("position", flux::Vec3f{1.5F, 2.0F, 2.5F});
    lightProperties.set("intensity", flux::Spectrum{40.0F, 40.0F, 40.0F});
    (void)context->create<flux::PointLight>(std::move(lightProperties));

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{2.5F, 2.0F, 2.5F});
    cameraProperties.set("look_at", flux::Vec3f{0.35F, 0.25F, 0.35F});
    cameraProperties.set("fov", 35.0F);
    auto camera = flux::Camera::create(std::move(cameraProperties));

    kira::Properties productProperties;
    productProperties.set("width", std::uint32_t{512});
    productProperties.set("height", std::uint32_t{512});
    productProperties.set("num_samples", std::uint32_t{32});
    auto product = flux::RenderProduct::create(camera, std::move(productProperties));
    product->getFilm().setChannels(flux::FilmChannels::Color);

    auto const outputDirectory = std::filesystem::path{argv[2]};
    flux::EmbreeHandler embree{context};
    embree.render(*product, product->getSamplesPerPixel());
    embree.download(*product);
    flux::writeImage<flux::ColorChannel>(outputDirectory / "embree.exr", product->getFilm());

    flux::OptixHandler optix{context, std::filesystem::path{FLUX_OPTIX_IR}};
    optix.render(*product, product->getSamplesPerPixel());
    optix.download(*product);
    flux::writeImage<flux::ColorChannel>(outputDirectory / "optix.exr", product->getFilm());
} catch (std::exception const &exception) {
    std::cerr << "flux: " << exception.what() << '\n';
    return 1;
}
