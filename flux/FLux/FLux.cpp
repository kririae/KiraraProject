#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>

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
#include "kira/Anyhow.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef FLUX_RELATIVE_OPTIX_IR
#error "FLUX_RELATIVE_OPTIX_IR must name the OptiX IR relative to the executable"
#endif
#ifndef FLUX_RELATIVE_MATERIALX_DIR
#error "FLUX_RELATIVE_MATERIALX_DIR must name the MaterialX root relative to the executable"
#endif

namespace {
/// \brief The path of the running executable.
[[nodiscard]] std::filesystem::path getMainExecutable() {
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        auto const size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (size == 0)
            throw kira::Anyhow(
                "failed to get the path of the executable: error {}", GetLastError()
            );
        if (size < path.size()) {
            path.resize(size);
            return path;
        }
        path.resize(path.size() * 2);
    }
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}

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
        result += handler.render(camera, buffer, batchSize);
    }
    return result;
}

int run(int argc, char **argv) {
    auto const options = flux::parseFLuxCLI(argc, argv);
    flux::configureLogging(options);
    auto const executableDir = getMainExecutable().parent_path();
    auto scene = flux::loadTomlScene(options, executableDir / FLUX_RELATIVE_MATERIALX_DIR);

    if (scene.film.resolution.x() == 0 || scene.film.resolution.y() == 0) {
        flux::LogInfo(
            "'{}' has a film of no pixels, nothing to render", options.scenePath.string()
        );
        kira::LogFlush<"flux">();
        return 0;
    }

    flux::Film film{scene.film};
    auto const *backend = options.backend == flux::RenderBackend::Optix ? "OptiX" : "Embree";
    flux::LogInfo(
        "rendering '{}' with {} at {}x{}, {} sample(s) per pixel", options.scenePath.string(),
        backend, film.getWidth(), film.getHeight(), scene.samplesPerPixel
    );

    // Each buffer is destroyed before its handler.
    auto const stats = [&] {
        if (options.backend == flux::RenderBackend::Optix) {
            flux::OptixHandler handler{scene.context, executableDir / FLUX_RELATIVE_OPTIX_IR};
            if (options.launchWaves)
                handler.setLaunchWaves(*options.launchWaves);
            flux::LogDebug(
                "OptixHandler: estimated resident threads {}", handler.getEstimatedResidentThreads()
            );
            flux::OptixAccumulationBuffer buffer{handler, scene.film};
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
        flux::EmbreeAccumulationBuffer buffer{scene.film};
        auto const result = renderUntil(
            handler, buffer, *scene.camera, scene.samplesPerPixel,
            std::numeric_limits<std::uint32_t>::max()
        );
        if (scene.samplesPerPixel != 0)
            buffer.download(film);
        return result;
    }();

    auto outputPath = options.outputPath.value_or(options.scenePath);
    if (!options.outputPath)
        outputPath.replace_extension(".exr");
    flux::writeImage<flux::ColorChannel>(outputPath, film);

    flux::LogInfo("wrote '{}'", outputPath.string());
    stats.log();
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
