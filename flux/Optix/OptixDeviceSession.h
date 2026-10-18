#pragma once

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <optix_types.h>

#include <cstdint>

#include "flux/Core/Object.h"

namespace flux {
/// \brief The CUDA device, stream, and OptiX device context that one handler runs on.
///
/// Never changes the calling thread's current context. A resource that uses the stream must be
/// destroyed before the session, with \c getPrimaryContext() current.
class OptixDeviceSession final : private Noncopyable {
public:
    /// \brief Opens a session on CUDA device \p deviceOrdinal.
    /// \throw kira::Anyhow If:
    /// - \p deviceOrdinal is outside [0, device count), or
    /// - CUDA or OptiX fails.
    explicit OptixDeviceSession(int deviceOrdinal);

    /// \brief Waits for the work queued on the stream, then closes the session.
    ~OptixDeviceSession();

    /// \brief Returns the owned OptiX device context.
    [[nodiscard]] OptixDeviceContext get() const noexcept { return context_; }

    /// \brief Returns the stream used by every dependent resource.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return stream_; }

    /// \brief Returns the estimated number of megakernel threads the device keeps resident.
    [[nodiscard]] std::uint32_t getEstimatedResidentThreads() const noexcept {
        return estimatedResidentThreads_;
    }

    /// \brief Returns the retained primary context of the device.
    [[nodiscard]] CUcontext getPrimaryContext() const noexcept { return primaryContext_; }

private:
    struct EmptyState {};

    explicit OptixDeviceSession(EmptyState) noexcept {}

    /// Null, or the primary context of \c device_ with one reference held by this session.
    CUcontext primaryContext_{};
    CUdevice device_{};

    /// Null, or a non-blocking stream in \c primaryContext_.
    cudaStream_t stream_{};

    /// Null, or an OptiX device context in \c primaryContext_ that reports through the FLux log.
    OptixDeviceContext context_{};
    std::uint32_t estimatedResidentThreads_{};
};
} // namespace flux
