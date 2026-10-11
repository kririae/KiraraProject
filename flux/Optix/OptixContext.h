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
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/OptixImageTexture.h"
#include "flux/Optix/OptixLightSampler.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTable.h"
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
    /// The host \c Context, OptiX device context, and deletion queue are borrowed from the owning
    /// handler and must outlive this object. All work runs on the stream of the deletion queue.
    OptixContext(
        Context &context, OptixDeviceContext deviceContext, DeletionQueue &deletionQueue,
        std::filesystem::path const &modulePath
    );

    /// \brief Brings the OptiX scene up to date with the host \c Context.
    ///
    /// If sync throws, this context is valid only for destruction.
    /// \pre Launches that read this context run on the stream of the deletion queue.
    void sync();

    /// \brief Launches \p size ray-generation work items on \p stream.
    ///
    /// \p params addresses \p paramsSize bytes of device memory.
    void launch(
        cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t size
    ) const;

    [[nodiscard]] Impl getImpl() const noexcept;
    [[nodiscard]] OptixProgramSpec const &getProgramSpec() const noexcept;

    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};

/// \brief OptiX scene used by device programs.
///
/// OptixContext keeps the referenced memory and handles valid for work queued on its stream
/// before its next sync.
struct OptixContext::Impl {
    struct Hit {
        SurfaceInteraction surface;
    };

    /// Top-level instance acceleration structure, or null when the scene has no instance.
    ///
    /// A traversal from a null handle misses
    /// (https://forums.developer.nvidia.com/t/how-to-use-optix-trace-a-child-instance/272378).
    OptixTraversableHandle traversable{};

    /// Instance index in the IAS of each primitive, by primitive index. A hole has
    /// no instance, and its entry is unused.
    std::uint32_t const *instanceIndices{};

    /// Scene tables in device memory.
    SceneTable table{};

    /// Image textures by image-texture index.
    OptixImageTextureTable imageTextures{};

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
