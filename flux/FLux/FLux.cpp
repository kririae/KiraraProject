#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>

#include "flux/Core/Logging.h"
#include "flux/Embree/EmbreeHandler.h"
#include "flux/FLux/FLuxCLI.h"
#include "flux/FLux/TomlScene.h"
#include "flux/IO/ImageIO.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/RenderProduct.h"

#ifndef FLUX_OPTIX_IR
#error "FLUX_OPTIX_IR must name the Flux OptiX IR module"
#endif

namespace {
int run(int argc, char **argv) {
    auto const request = flux::parseFluxCLI(argc, argv);
    flux::configureLogging(request);
    auto scene = flux::loadTomlScene(request);

    auto const &film = scene.product->getFilm();
    auto const *backend = request.backend == flux::RenderBackend::Optix ? "OptiX" : "Embree";
    flux::LogInfo(
        "rendering '{}' with {} at {}x{}, {} sample(s) per pixel", request.scenePath.string(),
        backend, film.getWidth(), film.getHeight(), scene.product->getSamplesPerPixel()
    );

    auto const stats = [&] {
        if (request.backend == flux::RenderBackend::Optix) {
            flux::OptixHandler handler{scene.context, std::filesystem::path{FLUX_OPTIX_IR}};
            flux::RenderStats result;
            auto remaining = scene.product->getSamplesPerPixel();
            // A full batch assigns one pixel's samples to one warp.
            constexpr auto launchBatchSize = 32U;
            while (remaining != 0) {
                auto const batchSize = std::min(remaining, launchBatchSize);
                auto const batch = handler.render(*scene.product, batchSize);
                result.paths += batch.paths;
                result.elapsed += batch.elapsed;
                remaining -= batchSize;
            }
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
    auto const pathsPerSecond = stats.getPathsPerSecond();
    flux::LogInfo(
        "rendered {} camera paths in {:.1f} ms at {:.1f} Mpaths/s; wrote '{}'", stats.paths,
        elapsed.count(), pathsPerSecond / 1.0e6, outputPath.string()
    );
    fmt::print(
        "rendering: {} camera paths in {:.1f} ms, {:.1f} Mpaths/s\n", stats.paths, elapsed.count(),
        pathsPerSecond / 1.0e6
    );
    kira::LogFlush<"flux">();
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (std::exception const &exception) {
        if (spdlog::get("flux")) {
            flux::LogError("{}", exception.what());
            kira::LogFlush<"flux">();
        } else {
            fmt::print(stderr, "[flux] [E] {}\n", exception.what());
        }
        return 1;
    }
}
