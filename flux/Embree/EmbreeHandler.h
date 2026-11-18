#pragma once

#include <cstdint>
#include <memory>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"

namespace flux {
class Camera;
class Context;
class EmbreeAccumulationBuffer;

/// \brief Schedules CPU rendering with an Embree scene.
///
/// The handler keeps its host \c Context.
///
/// \par Failure
/// An exception that escapes \c sync or \c render makes the handler failed for good.
/// Create a new handler to render again.
///
/// \remark A handler is not thread-safe.
/// Different handlers may render concurrently.
class EmbreeHandler final : private Noncopyable {
public:
    /// \brief Creates a handler and builds its Embree scene.
    ///
    /// \pre \p context is not null.
    /// \throw kira::Anyhow If the context has no active integrator or sampler, or Embree fails.
    explicit EmbreeHandler(Ref<Context> context);

    ~EmbreeHandler();

    /// \brief Rebuilds the Embree scene from the host \c Context.
    ///
    /// Does nothing if the context version has not changed since the last sync, or once the
    /// handler has failed.
    ///
    /// \throw kira::Anyhow If the context has no active integrator or sampler, or Embree fails.
    void sync();

    /// \brief Renders \p samples samples per pixel from \p camera into \p buffer.
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
    /// \throw kira::Anyhow If Embree fails.
    /// \return The camera paths that ran, and the time that the backend took.
    RenderStats
    render(Camera const &camera, EmbreeAccumulationBuffer &buffer, std::uint32_t samples);

public:
    /// \brief Returns whether an exception has escaped \c sync or \c render.
    ///
    /// Once true, it stays true.
    [[nodiscard]] bool hasFailed() const noexcept;

    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
