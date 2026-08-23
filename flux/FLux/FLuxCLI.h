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

/// \brief Options accepted by the Flux executable.
struct FluxCLIRequest {
    std::filesystem::path scenePath;
    std::optional<std::filesystem::path> outputPath;
    std::optional<std::filesystem::path> logFilePath;
    std::optional<std::uint32_t> samplesPerPixel;
    RenderBackend backend{RenderBackend::Optix};
    spdlog::level::level_enum logLevel{spdlog::level::warn};
};

/// \brief Parses the Flux command line.
///
/// Invalid arguments throw.
[[nodiscard]] FluxCLIRequest parseFluxCLI(int argc, char **argv);

/// \brief Configures CLI logging before renderer work starts.
void configureLogging(FluxCLIRequest const &request);
} // namespace flux
