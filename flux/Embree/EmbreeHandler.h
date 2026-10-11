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
    /// Does nothing if the context version has not changed since the last sync that succeeded.
    ///
    /// \throw kira::Anyhow If the context has no active integrator or sampler, or Embree fails.
    void sync();

    /// \brief Renders \p samples samples per pixel from \p camera into \p buffer.
    ///
    /// If \p samples is 0, nothing runs and \p buffer keeps its samples.
    ///
    /// Renders the scene as of the last \c sync.
    ///
    /// The new samples continue the sample sequence of \p buffer.
    /// If \p buffer has no samples, its old pixel values do not affect the result.
    /// A buffer with no channels still counts its samples.
    ///
    /// If the work fails, \p buffer has no samples and its pixels are unspecified.
    ///
    /// \pre The last sync succeeded.
    /// \throw kira::Anyhow If Embree fails.
    /// \return The camera paths that ran, and the time that the backend took.
    RenderStats
    render(Camera const &camera, EmbreeAccumulationBuffer &buffer, std::uint32_t samples);

public:
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
