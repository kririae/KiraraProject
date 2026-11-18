#pragma once

#include <cuda_runtime_api.h>
#include <optix_types.h>

#include <cstddef>
#include <span>
#include <vector>

#include "flux/Optix/DeviceBuffer.h"

namespace flux {
/// \brief One compacted geometry acceleration structure.
///
/// A triangle GAS does not read its build inputs after the build, so it may
/// outlive the buffers it was built from.
struct OptixGas {
    explicit OptixGas(cudaStream_t stream) noexcept : storage(stream) {}

    /// Traversable handle of the compacted GAS.
    OptixTraversableHandle handle{};
    DeviceBuffer<std::byte> storage;
};

/// \brief Builds and compacts one GAS per triangle input of \p inputs on \p stream.
///
/// Returns the GASes in input order. Compaction is queued on \p stream, so a GAS
/// is ready once \p stream completes.
/// \throw kira::Anyhow If an input is not a triangle input, exceeds the device
///        limit, or OptiX or CUDA fails.
[[nodiscard]] std::vector<OptixGas> buildGas(
    OptixDeviceContext deviceContext, cudaStream_t stream, std::span<OptixBuildInput const> inputs
);
} // namespace flux
