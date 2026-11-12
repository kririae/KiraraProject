#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"

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
/// \remark A handler is not thread-safe.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its OptiX scene.
    ///
    /// \p modulePath names the OptiX IR module that the handler uses.
    /// \p deviceOrdinal is the ordinal of the CUDA device that the handler runs on.
    ///
    /// \pre \p context is not null.
    /// \throw kira::Anyhow If \p deviceOrdinal is outside [0, device count), the context has no
    /// active integrator or sampler, or CUDA or OptiX fails.
    OptixHandler(
        Ref<Context> context, std::filesystem::path const &modulePath, int deviceOrdinal = 0
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

    /// \brief Renders \p samples samples per pixel from \p camera into \p buffer.
    ///
    /// Waits until the samples are in \p buffer.
    ///
    /// If \p samples is 0, or once the handler has failed, nothing runs and \p buffer keeps its
    /// samples.
    ///
    /// Renders the scene as of the last \c sync.
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
    /// The thread count is \p waves times the estimated number of resident threads.
    /// It changes how many threads share a render.
    /// It does not change how much work the render does.
    ///
    /// Use a value above 1 if the estimate leaves part of the device idle.
    /// A value below 1 can help on a device with a small L2 cache.
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
