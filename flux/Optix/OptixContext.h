#pragma once

#include <cuda_runtime_api.h>
#include <optix_types.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>

#include "flux/Core/Object.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "kira/Compiler.h"

namespace flux {
class Context;
class OptixHandler;
struct OptixProgramSpec;

/// \brief Owns the OptiX materialization of a host scene.
class OptixContext final : private Noncopyable {
    friend class OptixHandler;

public:
    /// \brief Compact device implementation of the materialized scene.
    struct DeviceImpl;

    /// \brief Releases device-scene resources.
    ~OptixContext();

private:
    /// \brief Creates an empty device scene for \p context.
    ///
    /// \param context Host scene borrowed from the owning \c OptixHandler.
    /// \param deviceContext OptiX device context borrowed from the owning
    /// \c OptixHandler.
    /// \param stream CUDA stream that orders scene updates and launches.
    /// \param modulePath Path to an OptiX IR module containing
    /// \c __raygen__megakernel, \c __miss__radiance, and
    /// \c __closesthit__triangle.
    /// \throw kira::Anyhow if the module cannot be read or OptiX setup fails.
    OptixContext(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path const &modulePath
    );

    /// \brief Replaces the device snapshot with the current host scene.
    ///
    /// The current implementation performs a full rebuild. All work enqueued
    /// by the update completes before this function returns. If rebuilding
    /// fails, destroy this object without using it again.
    /// \throw kira::Anyhow If scene linking, CUDA, or OptiX setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown geometry.
    void sync();

    /// \brief Launches the persistent pipeline on \p stream.
    ///
    /// \param stream CUDA stream that orders the launch.
    /// \param params Device address of the launch parameters.
    /// \param paramsSize Size of the launch parameters in bytes.
    /// \param width Number of ray-generation work items along the X axis.
    /// \param height Number of ray-generation work items along the Y axis.
    void launch(
        cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t width,
        std::uint32_t height
    ) const;

    /// \brief Returns the current device-scene implementation.
    [[nodiscard]] DeviceImpl getDeviceImpl() const noexcept;

    /// \brief Returns the values specialized into the current pipeline.
    [[nodiscard]] OptixProgramSpec const &getProgramSpec() const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// \brief Device implementation of an OptiX scene materialization.
struct OptixContext::DeviceImpl {
    /// Top-level instance acceleration structure.
    OptixTraversableHandle traversable{};

    /// Device array of unique triangle meshes.
    TriangleMesh::DeviceImpl const *geometries{};

    /// Device array of visible primitives.
    Primitive::DeviceImpl const *primitives{};

    /// Number of elements in \c geometries.
    std::uint32_t numGeometries{};

    /// Number of elements in \c primitives.
    std::uint32_t numPrimitives{};

public:
    /// \brief Returns the primitive at dense \p instanceIndex.
    ///
    /// \pre \p instanceIndex is less than \c numPrimitives.
    [[nodiscard]] KIRA_DEVICE inline Primitive::DeviceImpl const &
    getPrimitive(std::uint32_t instanceIndex) const noexcept;

    /// \brief Returns the geometry at dense \p geometryIndex.
    ///
    /// \pre \p geometryIndex is less than \c numGeometries.
    [[nodiscard]] KIRA_DEVICE inline TriangleMesh::DeviceImpl const &
    getGeometry(std::uint32_t geometryIndex) const noexcept;
};

static_assert(std::is_standard_layout_v<OptixContext::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<OptixContext::DeviceImpl>);

namespace optix {
/// Device representation of an OptiX scene snapshot.
using Scene = ::flux::OptixContext::DeviceImpl;
} // namespace optix
} // namespace flux
