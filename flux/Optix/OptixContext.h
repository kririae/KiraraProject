#pragma once

#include <cuda_runtime_api.h>
#include <optix_types.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>

#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "flux/Optix/OptixImageTexturePool.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
class Context;
class OptixHandler;
struct OptixProgramSpec;

/// \brief Owns the OptiX scene built from a host \c Context.
class OptixContext final : private Noncopyable {
    friend class OptixHandler;

public:
    /// \brief Device view of the current OptiX scene.
    struct Impl;

    ~OptixContext();

private:
    /// \brief Creates an empty OptiX scene for \p context.
    ///
    /// The host \c Context, OptiX device context, and CUDA stream are borrowed
    /// from the owning handler and must outlive this object.
    OptixContext(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path const &modulePath
    );

    /// \brief Commits the host \c Context and rebuilds the OptiX scene.
    ///
    /// Rebuilding clears the current scene first and waits for queued work. A
    /// failed sync leaves this context unusable.
    void sync();

    /// \brief Launches \p size ray-generation work items on \p stream.
    ///
    /// \p params addresses \p paramsSize bytes of device memory.
    void launch(
        cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t size
    ) const;

    [[nodiscard]] Impl getImpl() const noexcept;
    [[nodiscard]] OptixProgramSpec const &getProgramSpec() const noexcept;

    struct Storage;
    std::unique_ptr<Storage> storage_;
};

/// \brief Device view of an OptiX scene.
///
/// The owning OptixContext keeps every referenced device array and OptiX
/// handle valid until its next sync or destruction.
struct OptixContext::Impl {
    struct Hit {
        SurfaceInteraction surface;
    };

    /// Top-level instance acceleration structure.
    OptixTraversableHandle traversable{};

    /// Device array of unique geometries.
    Geometry::Impl const *geometries{};

    /// Device array of visible primitives.
    Primitive::Impl const *primitives{};

    /// Device array of BSDFs registered in the host \c Context.
    BSDF::Impl const *bsdfs{};

    /// Device array of EDFs registered in the host \c Context.
    EDF::Impl const *edfs{};

    OptixImageTexturePool::Impl imageTexturePool{};

    /// Borrowed light sampler.
    LightSampler lightSampler{};

    std::uint32_t numGeometries{}; // *geometries
    std::uint32_t numPrimitives{}; // *primitives
    std::uint32_t numBSDFs{};      // *bsdfs
    std::uint32_t numEDFs{};       // *edfs

public:
    /// \brief Finds the closest surface hit for \p ray.
    ///
    /// Reordering groups hits before their surface data is materialized. Returns
    /// false when the scene is empty or the ray misses.
    [[nodiscard]] KIRA_DEVICE bool
    intersect(Ray const &ray, Hit &hit, bool shaderReorder) const noexcept;

    /// \brief Returns whether \p ray reaches its endpoint without obstruction.
    [[nodiscard]] KIRA_DEVICE bool isVisible(Ray const &ray) const noexcept;

    /// \brief Maps a geometry-space point through primitive \p primitiveIndex.
    [[nodiscard]] KIRA_DEVICE Vec3f
    transformPointToWorld(std::uint32_t primitiveIndex, Vec3f const &point) const noexcept;

    /// \brief Maps a geometry-space normal through primitive \p primitiveIndex.
    [[nodiscard]] KIRA_DEVICE Vec3f
    transformNormalToWorld(std::uint32_t primitiveIndex, Vec3f const &normal) const noexcept;

    /// \brief Returns the primitive at dense \p instanceIndex.
    ///
    /// \pre \p instanceIndex is less than \c numPrimitives.
    [[nodiscard]] KIRA_DEVICE inline Primitive::Impl const &
    getPrimitive(std::uint32_t instanceIndex) const noexcept;

    /// \brief Returns the geometry at dense \p geometryIndex.
    ///
    /// \pre \p geometryIndex is less than \c numGeometries.
    [[nodiscard]] KIRA_DEVICE inline Geometry::Impl const &
    getGeometry(std::uint32_t geometryIndex) const noexcept;

    /// \brief Returns the BSDF at dense \p bsdfIndex.
    ///
    /// \pre \p bsdfIndex is less than \c numBSDFs.
    [[nodiscard]] KIRA_DEVICE inline BSDF::Impl const &
    getBSDF(std::uint32_t bsdfIndex) const noexcept;

    [[nodiscard]] KIRA_DEVICE inline EDF::Impl const &getEDF(std::uint32_t edfIndex) const noexcept;

    [[nodiscard]] KIRA_DEVICE inline LightSampler const &getLightSampler() const noexcept {
        return lightSampler;
    }
};

static_assert(std::is_standard_layout_v<OptixContext::Impl>);
static_assert(std::is_trivially_copyable_v<OptixContext::Impl>);

namespace optix {
/// OptiX device scene view.
using Scene = ::flux::OptixContext::Impl;
} // namespace optix
} // namespace flux
