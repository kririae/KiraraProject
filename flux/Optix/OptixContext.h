#pragma once

#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"

struct OptixDeviceContext_t;
using OptixDeviceContext = OptixDeviceContext_t *;

struct OptixPipeline_t;
using OptixPipeline = OptixPipeline_t *;

struct OptixShaderBindingTable;

namespace flux {
class Context;
class OptixHandler;

/// \brief Owns the persistent OptiX representation of a host scene.
///
/// The current pipeline contains one ray-generation program and its shader
/// binding table record.
class OptixContext final : private Noncopyable {
    friend class OptixHandler;

public:
    /// \brief Creates a device-scene representation of \p context.
    ///
    /// \param context Host scene mirrored by this object.
    /// \param deviceContext OptiX device context borrowed from the owning
    /// \c OptixHandler.
    /// \param modulePath Path to an OptiX IR module containing
    /// \c __raygen__megakernel.
    /// \throw kira::Anyhow if the module cannot be read or OptiX setup fails.
    OptixContext(
        Context &context, OptixDeviceContext deviceContext, std::filesystem::path const &modulePath
    );

    /// \brief Releases device-scene resources.
    ~OptixContext();

private:
    /// \brief Returns the pipeline owned by this device scene.
    [[nodiscard]] OptixPipeline getPipeline() const noexcept;

    /// \brief Returns the shader binding table owned by this device scene.
    [[nodiscard]] OptixShaderBindingTable const &getSbt() const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
