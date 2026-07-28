#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Holds the OptiX state needed for individual launches.
///
/// The handler retains the host scene and owns the OptiX device context,
/// launch stream, and launch-time state on CUDA device 0. Persistent
/// device-scene resources belong to its \c OptixContext. Public operations
/// select device 0 before touching backend resources. Call \c sync after
/// changing the host scene and before launching work that must observe those
/// changes.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates an OptiX handler using the program in \p modulePath.
    ///
    /// \param context Host context retained for the lifetime of the handler.
    /// \param modulePath Path to the OptiX IR module used by the pipeline.
    /// \throw kira::Anyhow if \p context is null, has no active integrator or
    /// sampler, or setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown geometry.
    OptixHandler(Ref<Context> context, std::filesystem::path const &modulePath);

    /// \brief Waits for pending work and releases device resources.
    ~OptixHandler();

    /// \brief Rebuilds the device scene from the associated host context.
    ///
    /// The current implementation performs a full rebuild and waits for all
    /// work enqueued by the update before returning. If rebuilding fails,
    /// destroy this handler without using it again.
    /// \throw kira::Anyhow If scene linking, CUDA, or OptiX setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown geometry.
    void sync();

    /// \brief Renders one sample batch into \p product and waits for completion.
    ///
    /// The active sampler belongs to the handler's host context. Call \c sync
    /// after changing persistent scene or program-specialization state.
    /// \param product Render product receiving the sample.
    /// \param samples Number of samples assigned to every pixel.
    /// \throw kira::Anyhow if there is no active sampler, program
    /// specialization is stale, device state creation fails, or the launch
    /// or stream synchronization fails.
    /// \throw std::invalid_argument If \p samples is zero, arithmetic
    /// overflows, or the launch is too large.
    void render(RenderProduct const &product, std::uint32_t samples);

    /// \brief Sets the sequence offset used by later render batches.
    ///
    /// Changing the offset invalidates every accumulated target.
    void setSampleOffset(std::uint64_t offset);

    /// \brief Returns the samples accumulated for \p product.
    ///
    /// A changed camera or film layout reports zero.
    /// \throw kira::Anyhow If the current camera cannot be materialized.
    [[nodiscard]] std::uint64_t getAccumulatedSamples(RenderProduct const &product) const;

    /// \brief Returns whether \p product has reached its target sample count.
    ///
    /// Invalid accumulation is never converged. Changing the target sample
    /// count does not discard samples already accumulated.
    /// \throw kira::Anyhow If the current camera cannot be materialized.
    [[nodiscard]] bool isConverged(RenderProduct const &product) const;

    /// \brief Releases backend state associated with \p product.
    ///
    /// The host product remains valid. Rendering it again creates fresh
    /// backend state.
    void release(RenderProduct const &product) noexcept;

    /// \brief Returns the associated host context.
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
