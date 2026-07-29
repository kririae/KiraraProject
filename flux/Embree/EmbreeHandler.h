#pragma once

#include <cstdint>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Schedules CPU rendering with an Embree scene.
///
/// The handler retains its host \c Context. Call \c sync after changing that
/// Context. Different handlers may render concurrently. Serialize mutation and
/// rendering on one handler.
class EmbreeHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its initial Embree scene.
    explicit EmbreeHandler(Ref<Context> context);

    ~EmbreeHandler();

    /// \brief Commits the host \c Context and rebuilds the Embree scene.
    ///
    /// Rebuilding clears the current scene first. A failed sync leaves this
    /// handler unusable. Sync also clears accumulation for every render product.
    /// Recover by creating a new handler.
    void sync();

    /// \brief Renders one sample batch into \p product.
    ///
    /// \p samples is the nonzero number of samples assigned to each pixel.
    /// A product with no requested channels still advances its sample count.
    void render(RenderProduct const &product, std::uint32_t samples);

    /// \brief Downloads every requested channel of \p product into its Film.
    ///
    /// The call completes synchronously.
    /// \throw kira::Anyhow If \p product has no valid accumulation.
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

    /// \brief Returns whether \p product reached its target sample count.
    ///
    /// Changing the target keeps valid accumulated samples.
    [[nodiscard]] bool isConverged(RenderProduct const &product) const;

    /// \brief Releases runtime storage for \p product.
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
