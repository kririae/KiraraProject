#pragma once

#include <optix_types.h>

#include <cstddef>
#include <span>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
/// \brief Owns the geometry acceleration structure for an OptiX device scene.
class OptixAccel final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Binds the acceleration structure to \p deviceContext and \p stream.
    OptixAccel(OptixDeviceContext deviceContext, cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), deviceContext_(deviceContext), output_(stream) {}

    /// \brief Rebuilds the GAS from \p inputs.
    ///
    /// Existing storage is released before the replacement is built.
    /// \param inputs Triangle build inputs whose device storage remains alive.
    /// \throw kira::Anyhow If OptiX or CUDA setup fails.
    void build(std::span<OptixBuildInput const> inputs);

    /// \brief Returns the current traversable handle, or zero when empty.
    [[nodiscard]] OptixTraversableHandle getHandle() const noexcept { return handle_; }

private:
    OptixDeviceContext deviceContext_;
    DeviceBuffer<std::byte> output_;
    OptixTraversableHandle handle_{};
};
} // namespace flux
