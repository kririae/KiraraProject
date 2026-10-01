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
#include "flux/Core/RayFootprint.h"
#include "flux/Optix/OptixImageTexturePool.h"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTable.h"
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

/// \brief OptiX scene used by device programs.
///
/// OptixContext keeps the referenced memory and handles valid until its next
/// sync.
struct OptixContext::Impl {
    struct Hit {
        SurfaceInteraction surface;
    };

    /// Top-level instance acceleration structure.
    OptixTraversableHandle traversable{};

    /// Dense scene tables in device memory.
    SceneTable table{};

    OptixImageTexturePool::Impl imageTexturePool{};

    OptixLightSampler::Impl lightSampler{};

public:
    /// \brief Finds the closest surface hit for \p ray.
    ///
    /// Reordering groups hits before their surface data is materialized. Returns
    /// false when the scene is empty or the ray misses.
    [[nodiscard]] KIRA_DEVICE bool
    intersect(Ray const &ray, Hit &hit, bool shaderReorder) const noexcept;

    /// \brief Returns whether \p ray reaches its endpoint without obstruction.
    ///
    /// Only instances whose visibility mask shares a bit with \p mask can block \p ray. A zero
    /// \p mask visits no instance and returns true.
    [[nodiscard]] KIRA_DEVICE bool
    isVisible(Ray const &ray, unsigned int mask = 255) const noexcept;

    /// \brief Maps a geometry-space point through primitive \p primitiveIndex.
    [[nodiscard]] KIRA_DEVICE Vec3f
    transformPointToWorld(std::uint32_t primitiveIndex, Vec3f const &point) const noexcept;

    /// \brief Maps a geometry-space normal through primitive \p primitiveIndex.
    [[nodiscard]] KIRA_DEVICE Vec3f
    transformNormalToWorld(std::uint32_t primitiveIndex, Vec3f const &normal) const noexcept;

    /// \brief Returns the PDF of sampling \p isect from \p ctx.
    ///
    /// The PDF includes light selection.
    [[nodiscard]] KIRA_DEVICE float
    pdfDirectLight(LightSamplingContext const &ctx, SurfaceInteraction const &isect) const noexcept;
};

static_assert(std::is_standard_layout_v<OptixContext::Impl>);
static_assert(std::is_trivially_copyable_v<OptixContext::Impl>);

namespace optix {
/// OptiX scene used by device programs.
using Scene = ::flux::OptixContext::Impl;
} // namespace optix
} // namespace flux
