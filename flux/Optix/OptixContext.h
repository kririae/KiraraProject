#pragma once

#include <cuda_runtime_api.h>
#include <optix_types.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class OptixHandler;

/// \brief Owns the OptiX materialization of a host scene.
class OptixContext final : private Noncopyable {
    friend class OptixHandler;

public:
    /// \brief Releases device-scene resources.
    ~OptixContext();

private:
    /// \brief Creates an empty device scene using \p deviceContext.
    ///
    /// \param deviceContext OptiX device context borrowed from the owning
    /// \c OptixHandler.
    /// \param stream CUDA stream that orders scene updates and launches.
    /// \param modulePath Path to an OptiX IR module containing
    /// \c __raygen__megakernel, \c __miss__intersection, and
    /// \c __closesthit__triangle.
    /// \throw kira::Anyhow if the module cannot be read or OptiX setup fails.
    OptixContext(
        OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path const &modulePath
    );

    /// \brief Replaces the device snapshot with the current host scene.
    ///
    /// The current implementation performs a full rebuild. All work enqueued
    /// by the update completes before this function returns. If rebuilding
    /// fails, destroy this object without using it again.
    /// \param context Host scene to commit and upload.
    /// \throw kira::Anyhow If scene linking, CUDA, or OptiX setup fails.
    void sync(Context &context);

    /// \brief Launches the persistent pipeline on \p stream.
    ///
    /// \param stream CUDA stream that orders the launch.
    /// \param params Device address of the launch parameters.
    /// \param paramsSize Size of the launch parameters in bytes.
    /// \param width Number of ray-generation work items.
    void launch(
        cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t width
    ) const;

    /// \brief Returns the scene's geometry traversable, or zero when empty.
    [[nodiscard]] OptixTraversableHandle getTraversable() const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace flux
