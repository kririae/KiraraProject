#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"
#include "flux/Optix/OptixMegaKernelIr.h"

namespace flux {
class Camera;
class Context;
class OptixDeviceSession;
class OptixAccumulationBuffer;

/// \brief Renders a host \c Context with OptiX on one CUDA device.
///
/// Public operations do not need a current CUDA context.
/// They leave the current CUDA context of the calling thread unchanged.
///
/// \par Failure
/// An exception that escapes \c sync or \c render makes the handler failed for good.
/// Create a new handler to render again.
///
/// \remark Calls to a handler do not overlap, unless a function states otherwise.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its OptiX scene.
    ///
    /// \p deviceOrdinal is the ordinal of the CUDA device that the handler runs on.
    /// \p megaKernelIr is the OptiX IR of the megakernel, which must outlive the handler.
    ///
    /// \pre \p context is not null.
    /// \throw kira::Anyhow If \p deviceOrdinal is outside [0, device count), the context has no
    /// active integrator or sampler, or CUDA or OptiX fails.
    explicit OptixHandler(
        Ref<Context> context, int deviceOrdinal = 0,
        std::span<char const> megaKernelIr = getOptixMegaKernelIr()
    );

    ~OptixHandler();

    /// \brief Updates the OptiX scene from the host \c Context.
    ///
    /// Does nothing if the context version has not changed since the last sync, or once the
    /// handler has failed.
    ///
    /// \throw kira::Anyhow If the context has no active integrator or sampler, or CUDA or OptiX
    /// fails.
    void sync();

    /// \brief Returns the version of the context that the OptiX scene holds.
    ///
    /// It may overlap a render.
    [[nodiscard]] std::uint64_t getSyncedVersion() const noexcept;

    /// \brief Renders \p samples samples per pixel from \p camera into \p buffer.
    ///
    /// Waits until the samples are in \p buffer.
    ///
    /// If \p samples is 0, or once the handler has failed, nothing runs and \p buffer keeps its
    /// samples.
    ///
    /// Renders the scene as of the last \c sync.
    /// The context may be edited and collected while it runs.
    ///
    /// The new samples continue the sample sequence of \p buffer.
    /// If \p buffer has no samples, its old pixel values do not affect the result.
    /// A buffer with no channels still counts its samples.
    ///
    /// If \c render throws, \p buffer keeps its samples or has none.
    ///
    /// \pre \p buffer was created by this handler.
    /// \throw std::length_error If the pixel count of \p buffer times \p samples is more than
    /// 2^32 - 1.
    /// \throw kira::Anyhow If CUDA or OptiX fails.
    /// \return The camera paths that ran, and the time that the backend took.
    RenderStats
    render(Camera const &camera, OptixAccumulationBuffer &buffer, std::uint32_t samples);

public:
    /// \brief Sets how many threads later launches use.
    ///
    /// \pre \p waves is positive.
    void setLaunchWaves(double waves);

    /// \brief Returns the estimated number of threads that the device keeps resident for a launch.
    ///
    /// The estimate does not include the launch waves.
    [[nodiscard]] std::uint32_t getEstimatedResidentThreads() const;

    /// \brief Returns whether an exception has escaped \c sync or \c render.
    ///
    /// Once true, it stays true.
    [[nodiscard]] bool hasFailed() const noexcept;

    [[nodiscard]] Ref<Context> getContext() const;

private:
    friend class OptixAccumulationBuffer;

    [[nodiscard]] OptixDeviceSession const &getDeviceSession() const noexcept;

    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
