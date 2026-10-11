#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Renders a host \c Context with OptiX on one CUDA device.
///
/// Public operations do not need a current CUDA context.
/// They leave the current CUDA context of the calling thread unchanged.
///
/// Call \c sync after you change the host \c Context.
///
/// \remark A handler is not thread-safe.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its OptiX scene.
    ///
    /// \p modulePath names the OptiX IR module that the handler uses.
    /// \p deviceOrdinal is the ordinal of the CUDA device that the handler runs on.
    ///
    /// \throw kira::Anyhow If \p deviceOrdinal is outside [0, device count).
    OptixHandler(
        Ref<Context> context, std::filesystem::path const &modulePath, int deviceOrdinal = 0
    );

    ~OptixHandler();

    /// \brief Updates the OptiX scene from the host \c Context.
    ///
    /// Does nothing if the context version has not changed since the last sync that succeeded.
    /// Otherwise it updates the scene and clears the accumulation of every render product.
    ///
    /// If sync throws, do not render until a later sync succeeds.
    void sync();

    /// \brief Renders one batch of samples into \p product.
    ///
    /// Waits until the batch is done.
    ///
    /// \p samples is the number of samples for each pixel. It must not be 0.
    /// A product with no requested channels still counts its samples.
    ///
    /// If the device work fails, the accumulation of \p product is cleared.
    ///
    /// \pre The last sync succeeded.
    /// \return The camera paths that ran, and the time that the backend took.
    RenderStats render(RenderProduct const &product, std::uint32_t samples);

    /// \brief Downloads every requested channel of \p product into its Film.
    ///
    /// Waits until the copy is done.
    ///
    /// \warning If allocation or the copy fails, the Film in \p product is valid only for
    /// destruction, swap, or move assignment.
    ///
    /// \throw kira::Anyhow If \p product has no valid accumulation.
    void download(RenderProduct &product);

public:
    /// \brief Sets how many threads later launches use.
    ///
    /// The thread count is \p waves times the estimated number of resident threads.
    /// It changes how many threads share a batch. It does not change how much work the batch does.
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

    /// \brief Sets the offset of the sample sequence for later renders.
    ///
    /// Changing the offset clears the accumulation of every render product.
    void setSampleOffset(std::uint64_t offset);

    /// \brief Returns the number of samples accumulated for \p product.
    ///
    /// Returns 0 if \p product has no accumulation.
    /// Returns 0 if its Camera, Film resolution, or channels changed since the last render.
    [[nodiscard]] std::uint64_t getAccumulatedSamples(RenderProduct const &product) const;

    /// \brief Returns whether \p product has reached its target sample count.
    ///
    /// Only valid accumulation counts.
    /// Changing the target keeps the accumulated samples.
    [[nodiscard]] bool isConverged(RenderProduct const &product) const;

    /// \brief Releases the film storage and the accumulation of \p product.
    ///
    /// Does nothing if there is no storage.
    ///
    /// \p product stays valid. A later render creates new storage.
    void release(RenderProduct const &product) noexcept;
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
