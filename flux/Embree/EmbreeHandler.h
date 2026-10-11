#pragma once

#include <cstdint>
#include <memory>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"

namespace flux {
class Context;
class RenderProduct;

/// \brief Schedules CPU rendering with an Embree scene.
///
/// The handler keeps its host \c Context.
/// Call \c sync after you change that Context.
///
/// \remark A handler is not thread-safe.
/// Different handlers may render concurrently.
class EmbreeHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its Embree scene.
    explicit EmbreeHandler(Ref<Context> context);

    ~EmbreeHandler();

    /// \brief Rebuilds the Embree scene from the host \c Context.
    ///
    /// Does nothing if the context version has not changed since the last sync that succeeded.
    /// Otherwise it also clears the accumulation of every render product.
    ///
    /// If sync throws, do not render until a later sync succeeds.
    void sync();

    /// \brief Renders one batch of samples into \p product.
    ///
    /// \p samples is the number of samples for each pixel.
    /// If \p samples is 0, nothing runs and \p product keeps its accumulation.
    ///
    /// A product with no requested channels still counts its samples.
    ///
    /// If the work fails, the accumulation of \p product is cleared.
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
