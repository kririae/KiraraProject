#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"

namespace flux {
class Context;

/// \brief Holds the OptiX state needed for individual launches.
///
/// The handler retains the host scene and owns the OptiX device context,
/// launch stream, and launch-time state. Persistent device-scene resources
/// belong to its \c OptixContext. Call \c sync after changing the host scene
/// and before launching work that must observe those changes.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates an OptiX handler using the program in \p modulePath.
    ///
    /// \param context Host context retained for the lifetime of the handler.
    /// \param modulePath Path to the OptiX IR module used by the pipeline.
    /// \throw kira::Anyhow if \p context is null or setup fails.
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

    /// \brief Launches the current pipeline and waits for completion.
    ///
    /// \throw kira::Anyhow if the launch or stream synchronization fails.
    void launch();

    /// \brief Intersects \p rays with the persistent device scene.
    ///
    /// \param rays Rays in world space.
    /// \return One result for each input ray, in input order.
    /// \throw kira::Anyhow If the request exceeds OptiX limits or the launch
    /// fails.
    [[nodiscard]] std::vector<RayHit> intersect(std::span<Ray const> rays);

    /// \brief Returns the associated host context.
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
