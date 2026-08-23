#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Schedules OptiX rendering and owns launch-time state.
///
/// The handler retains a host \c Context and owns the OptiX device context,
/// CUDA stream, and launch-time state on CUDA device 0. Its \c OptixContext
/// owns the persistent OptiX scene. Public operations select device 0. Call
/// \c sync after changing the host \c Context.
///
/// \remark A handler is not thread-safe.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its initial OptiX scene.
    ///
    /// \p modulePath names the OptiX IR module used by the pipeline.
    OptixHandler(Ref<Context> context, std::filesystem::path const &modulePath);

    ~OptixHandler();

    /// \brief Commits the host \c Context and rebuilds the OptiX scene.
    ///
    /// Rebuilding clears the current scene first and waits for queued work. A
    /// failed sync leaves this handler unusable. Sync also clears accumulation
    /// for every render product. Recover by creating a new handler.
    void sync();

    /// \brief Renders one sample batch into \p product and waits for completion.
    ///
    /// \p samples is the nonzero number of samples assigned to each pixel. Call
    /// \c sync after changing the host \c Context.
    /// A product with no requested channels still advances its sample count.
    /// If backend work fails, the product's accumulation is cleared.
    /// \return Executed camera paths and backend execution time.
    RenderStats render(RenderProduct const &product, std::uint32_t samples);

    /// \brief Downloads every requested channel of \p product into its Film.
    ///
    /// The handler stream orders the copies. This function waits for that
    /// stream before returning.
    /// \throw kira::Anyhow If \p product has no valid accumulation.
    /// \warning An exception while allocating or copying channel data leaves
    /// the Film in \p product valid only for destruction, swap, or move assignment.
    void download(RenderProduct &product);

public:
    /// \brief Sets the sequence offset used by later render batches.
    ///
    /// Changing the offset clears accumulation for every render product.
    void setSampleOffset(std::uint64_t offset);

    /// \brief Returns the samples accumulated for \p product.
    ///
    /// A missing entry or changed Camera, Film resolution, or channel request
    /// reports zero.
    [[nodiscard]] std::uint64_t getAccumulatedSamples(RenderProduct const &product) const;

    /// \brief Returns whether \p product has reached its target sample count.
    ///
    /// Valid accumulation converges at the target sample count. Changing the
    /// target sample count keeps the accumulated samples.
    [[nodiscard]] bool isConverged(RenderProduct const &product) const;

    /// \brief Releases the film buffers and accumulation state for \p product.
    ///
    /// Does nothing if no storage exists. The render product remains valid, and
    /// a later render creates new storage.
    void release(RenderProduct const &product) noexcept;
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
