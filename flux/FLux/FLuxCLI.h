#pragma once

#include <spdlog/common.h>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace flux {
/// \brief Renderer selected for one command-line invocation.
enum class RenderBackend { // NOLINT
    Optix,
    Embree,
};

/// \brief Options accepted by the FLux executable.
struct FLuxCLIOptions {
    std::filesystem::path scenePath;
    std::optional<std::filesystem::path> outputPath;
    std::optional<std::filesystem::path> logFilePath;
    std::optional<std::uint32_t> samplesPerPixel;
    std::optional<double> launchWaves;
    std::optional<std::uint32_t> chunkSize;
    RenderBackend backend{RenderBackend::Optix};
    spdlog::level::level_enum logLevel{spdlog::level::warn};
};

/// \brief Parses the FLux command line.
///
/// Invalid arguments throw.
[[nodiscard]] FLuxCLIOptions parseFLuxCLI(int argc, char **argv);

/// \brief Configures CLI logging before renderer work starts.
void configureLogging(FLuxCLIOptions const &options);
} // namespace flux
