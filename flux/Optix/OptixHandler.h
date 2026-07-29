#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Schedules OptiX rendering and owns launch-time state.
///
/// The handler retains a host \c Context and owns the OptiX device context,
/// CUDA stream, and launch-time state on CUDA device 0. Its \c OptixContext
/// owns the persistent OptiX scene. Public operations select device 0. Call
/// \c sync after changing the host \c Context.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates an OptiX handler using the program in \p modulePath.
    ///
    /// \param context Host \c Context retained for the lifetime of the handler.
    /// \param modulePath Path to the OptiX IR module used by the pipeline.
    /// \throw kira::Anyhow if \p context is null, has no active integrator or
    /// sampler, or setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown geometry.
    OptixHandler(Ref<Context> context, std::filesystem::path const &modulePath);

    /// \brief Waits for pending work and releases device resources.
    ~OptixHandler();

    /// \brief Rebuilds the OptiX scene from the host \c Context.
    ///
    /// This function fully rebuilds the OptiX scene and waits for queued work
    /// before returning. Destroy this handler after a failed rebuild.
    /// \throw kira::Anyhow If scene linking, CUDA, or OptiX setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown geometry.
    void sync();

    /// \brief Renders one sample batch into \p product and waits for completion.
    ///
    /// The host \c Context owns the active Sampler. Call \c sync after changing
    /// the Context or program specialization.
    /// \param product Render product receiving the sample.
    /// \param samples Number of samples assigned to every pixel.
    /// \throw kira::Anyhow If the Context has no active Sampler, the program
    /// specialization changed, or CUDA or OptiX reports an error.
    /// \throw std::invalid_argument If \p samples is zero, arithmetic
    /// overflows, or the launch is too large.
    void render(RenderProduct const &product, std::uint32_t samples);

    /// \brief Downloads every requested channel of \p product into its Film.
    ///
    /// The handler-owned stream orders each copy. This function waits for that
    /// stream and accepts any valid accumulation.
    /// After an exception modifies Film, destroy, swap, or move-assign Film.
    /// \throw kira::Anyhow If no valid accumulation exists or CUDA download
    /// fails.
    void download(RenderProduct &product);

    /// \brief Sets the sequence offset used by later render batches.
    ///
    /// Changing the offset clears accumulation for every render product.
    void setSampleOffset(std::uint64_t offset);

    /// \brief Returns the samples accumulated for \p product.
    ///
    /// A changed Camera, Film resolution, or channel request reports zero.
    /// \throw kira::Anyhow If the Camera parameters are invalid.
    [[nodiscard]] std::uint64_t getAccumulatedSamples(RenderProduct const &product) const;

    /// \brief Returns whether \p product has reached its target sample count.
    ///
    /// Valid accumulation converges at the target sample count. Changing the
    /// target sample count keeps the accumulated samples.
    /// \throw kira::Anyhow If the Camera parameters are invalid.
    [[nodiscard]] bool isConverged(RenderProduct const &product) const;

    /// \brief Erases runtime storage for \p product.
    ///
    /// The render product remains valid. Rendering it again creates new runtime
    /// storage.
    void release(RenderProduct const &product) noexcept;

    /// \brief Returns the host \c Context.
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
