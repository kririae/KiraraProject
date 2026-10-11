#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>

#include "flux/Core/Logging.h"
#include "flux/Core/RenderStats.h"
#include "flux/Embree/EmbreeAccumulationBuffer.h"
#include "flux/Embree/EmbreeHandler.h"
#include "flux/FLux/FLuxCLI.h"
#include "flux/FLux/TomlScene.h"
#include "flux/IO/ImageIO.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

#ifndef FLUX_OPTIX_IR
#error "FLUX_OPTIX_IR must name the Flux OptiX IR module"
#endif

namespace {
/// Renders batches of at most \p maxBatchSize samples until \p buffer holds \p target samples.
template <typename Handler, typename Buffer>
[[nodiscard]] flux::RenderStats renderUntil(
    Handler &handler, Buffer &buffer, flux::Camera const &camera, std::uint32_t target,
    std::uint32_t maxBatchSize
) {
    flux::RenderStats result;
    while (buffer.getSampleCount() < target) {
        auto const batchSize = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(target - buffer.getSampleCount(), maxBatchSize)
        );
        auto const batch = handler.render(camera, buffer, batchSize);
        result.paths += batch.paths;
        result.elapsed += batch.elapsed;
    }
    return result;
}

int run(int argc, char **argv) {
    auto const request = flux::parseFluxCLI(argc, argv);
    flux::configureLogging(request);
    auto scene = flux::loadTomlScene(request);

    if (scene.resolution.x() == 0 || scene.resolution.y() == 0) {
        flux::LogInfo(
            "'{}' has a film of no pixels, nothing to render", request.scenePath.string()
        );
        kira::LogFlush<"flux">();
        return 0;
    }

    flux::Film film{scene.resolution, scene.channels};
    auto const *backend = request.backend == flux::RenderBackend::Optix ? "OptiX" : "Embree";
    flux::LogInfo(
        "rendering '{}' with {} at {}x{}, {} sample(s) per pixel", request.scenePath.string(),
        backend, film.getWidth(), film.getHeight(), scene.samplesPerPixel
    );

    // Each buffer is destroyed before its handler.
    auto const stats = [&] {
        if (request.backend == flux::RenderBackend::Optix) {
            flux::OptixHandler handler{scene.context, std::filesystem::path{FLUX_OPTIX_IR}};
            if (request.launchWaves)
                handler.setLaunchWaves(*request.launchWaves);
            flux::LogDebug(
                "OptixHandler: estimated resident threads {}", handler.getEstimatedResidentThreads()
            );
            flux::OptixAccumulationBuffer buffer{handler, scene.resolution, scene.channels};
            // A render has at most 2^32 - 1 paths. A full batch of 32 assigns one pixel's samples
            // to one warp.
            auto const pixelCount = std::uint64_t{film.getWidth()} * film.getHeight();
            auto const launchBatchSize = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max() / pixelCount, 1, 32
            ));
            auto const result =
                renderUntil(handler, buffer, *scene.camera, scene.samplesPerPixel, launchBatchSize);
            if (scene.samplesPerPixel != 0)
                buffer.download(film);
            return result;
        }

        flux::EmbreeHandler handler{scene.context};
        flux::EmbreeAccumulationBuffer buffer{scene.resolution, scene.channels};
        auto const result = renderUntil(
            handler, buffer, *scene.camera, scene.samplesPerPixel,
            std::numeric_limits<std::uint32_t>::max()
        );
        if (scene.samplesPerPixel != 0)
            buffer.download(film);
        return result;
    }();

    auto outputPath = request.outputPath.value_or(request.scenePath);
    if (!request.outputPath)
        outputPath.replace_extension(".exr");
    flux::writeImage<flux::ColorChannel>(outputPath, film);

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
