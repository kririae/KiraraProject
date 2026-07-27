#pragma once

#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Context;

/// \brief Holds the OptiX state needed for individual launches.
///
/// The handler retains the host scene and owns the OptiX device context,
/// launch stream, and launch-time state. Persistent device-scene resources
/// belong to its \c OptixContext.
class OptixHandler final : private Noncopyable {
public:
    /// \brief Creates an OptiX handler using the program in \p modulePath.
    ///
    /// \param context Host context retained for the lifetime of the handler.
    /// \param modulePath Path to the OptiX IR module used by the pipeline.
    /// \throw kira::Anyhow if \p context is empty or setup fails.
    OptixHandler(Ref<Context> context, std::filesystem::path const &modulePath);

    /// \brief Waits for pending work and releases device resources.
    ~OptixHandler();

    /// \brief Launches the current pipeline and waits for completion.
    ///
    /// \throw kira::Anyhow if the launch or stream synchronization fails.
    void launch();

    /// \brief Returns the associated host context.
    [[nodiscard]] Ref<Context> getContext() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
