#include <exception>
#include <filesystem>
#include <iostream>

#include "flux/Embree/EmbreeHandler.h"
#include "flux/FLux/FLuxCLI.h"
#include "flux/FLux/TomlScene.h"
#include "flux/IO/ImageIO.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/RenderProduct.h"

#ifndef FLUX_OPTIX_IR
#error "FLUX_OPTIX_IR must name the Flux OptiX IR module"
#endif

int main(int argc, char **argv) try {
    auto const request = flux::parseFluxCLI(argc, argv);
    auto scene = flux::loadTomlScene(request);

    if (request.backend == flux::RenderBackend::Optix) {
        flux::OptixHandler handler{scene.context, std::filesystem::path{FLUX_OPTIX_IR}};
        handler.render(*scene.product, scene.product->getSamplesPerPixel());
        handler.download(*scene.product);
    } else {
        flux::EmbreeHandler handler{scene.context};
        handler.render(*scene.product, scene.product->getSamplesPerPixel());
        handler.download(*scene.product);
    }

    auto outputPath = request.outputPath.value_or(request.scenePath);
    if (!request.outputPath)
        outputPath.replace_extension(".exr");
    flux::writeImage<flux::ColorChannel>(outputPath, scene.product->getFilm());
} catch (std::exception const &exception) {
    std::cerr << "flux: " << exception.what() << '\n';
    return 1;
}
