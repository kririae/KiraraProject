#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace flux {
/// \brief Renderer selected for one command-line invocation.
enum class RenderBackend {
    Optix,
    Embree,
};

/// \brief Options accepted by the Flux executable.
struct FluxCLIRequest {
    std::filesystem::path scenePath;
    std::optional<std::filesystem::path> outputPath;
    std::optional<std::uint32_t> samplesPerPixel;
    RenderBackend backend{RenderBackend::Optix};
};

/// \brief Parses the Flux command line.
///
/// Invalid arguments throw.
[[nodiscard]] FluxCLIRequest parseFluxCLI(int argc, char **argv);
} // namespace flux
