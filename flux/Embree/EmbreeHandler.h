#pragma once

#include <cstdint>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Schedules CPU rendering with an Embree scene.
///
/// Call \c sync after changing the host \c Context. Different handlers may
/// render concurrently. Serialize mutation and rendering on one handler.
class EmbreeHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its initial Embree scene.
    ///
    /// \throw kira::Anyhow If \p context is null, has no active integrator or
    /// sampler, or Embree setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown object.
    explicit EmbreeHandler(Ref<Context> context);

    /// \brief Releases the Embree scene and runtime storage.
    ~EmbreeHandler();

    /// \brief Rebuilds the Embree scene from the host \c Context.
    ///
    /// This function clears the current Embree scene before rebuilding it.
    /// Destroy this handler after a failed rebuild.
    void sync();

    /// \brief Renders one sample batch into \p product.
    ///
    /// Each worker renders all samples for one pixel before writing Film.
    /// \throw kira::Anyhow If the Camera or Sampler parameters are invalid.
    /// \throw std::invalid_argument If \p samples is zero or arithmetic
    /// overflows.
    void render(RenderProduct const &product, std::uint32_t samples);

    /// \brief Downloads every requested channel of \p product into its Film.
    ///
    /// The call completes synchronously for any valid accumulation.
    /// After an exception modifies Film, destroy, swap, or move-assign Film.
    /// \throw kira::Anyhow If no valid accumulation exists.
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

    /// \brief Returns whether \p product reached its target sample count.
    ///
    /// \throw kira::Anyhow If the Camera parameters are invalid.
    [[nodiscard]] bool isConverged(RenderProduct const &product) const;

    /// \brief Erases runtime storage for \p product.
    void release(RenderProduct const &product) noexcept;

    /// \brief Returns the host \c Context.
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
