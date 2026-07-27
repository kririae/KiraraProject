#pragma once

#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Camera;
class Context;
class RenderProduct;

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

    /// \brief Renders \p camera into \p product and waits for completion.
    ///
    /// Both objects must belong to the handler's host context.
    /// \throw kira::Anyhow if the launch or stream synchronization fails.
    /// \throw std::invalid_argument if either object belongs to another
    /// context or the film is too large for device storage.
    void render(Camera const &camera, RenderProduct const &product);

    /// \brief Returns the associated host context.
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
