#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
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

    auto const stats = [&] {
        if (request.backend == flux::RenderBackend::Optix) {
            flux::OptixHandler handler{scene.context, std::filesystem::path{FLUX_OPTIX_IR}};
            auto result = handler.render(*scene.product, scene.product->getSamplesPerPixel());
            handler.download(*scene.product);
            return result;
        }

        flux::EmbreeHandler handler{scene.context};
        auto result = handler.render(*scene.product, scene.product->getSamplesPerPixel());
        handler.download(*scene.product);
        return result;
    }();

    auto outputPath = request.outputPath.value_or(request.scenePath);
    if (!request.outputPath)
        outputPath.replace_extension(".exr");
    flux::writeImage<flux::ColorChannel>(outputPath, scene.product->getFilm());

    using Milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;
    auto const elapsed = std::chrono::duration_cast<Milliseconds>(stats.elapsed);
    std::cout << std::fixed << std::setprecision(1) << "rendering: " << stats.paths
              << " camera paths in " << elapsed.count() << " ms, "
              << stats.getPathsPerSecond() / 1.0e6 << " Mpaths/s\n";
} catch (std::exception const &exception) {
    std::cerr << "flux: " << exception.what() << '\n';
    return 1;
}
