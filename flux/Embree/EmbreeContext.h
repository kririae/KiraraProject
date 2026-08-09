#pragma once

#include <embree4/rtcore.h>

#include <array>
#include <cstdint>
#include <vector>

#include "flux/Core/MathUtils.h"
#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "flux/Embree/EmbreeImageTexturePool.h"
#include "flux/Embree/EmbreeLightSampler.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Interaction.h"
#include "kira/SmallVector.h"

namespace flux {
class Context;

/// \brief Owns the Embree scene built from one host \c Context.
///
/// The Embree scene shares immutable mesh arrays with retained meshes and owns
/// its acceleration structures, scene tables, and instance normal transforms.
/// Call \c sync from one thread. Traversal supports concurrent calls after sync.
class EmbreeContext final : private Noncopyable {
public:
    struct Impl;

    /// \brief Embree intersection result.
    struct Hit {
        /// Geometry-space intersection data.
        PreliminaryIntersection preliminary;

        /// Unnormalized geometry-space normal reported by Embree.
        Vec3f geometricNormal;

        /// Dense primitive index in this Embree scene.
        std::uint32_t primitiveIndex;
    };

    /// \brief Creates an empty scene and its Embree device.
    ///
    /// \p context is borrowed and must outlive this object.
    explicit EmbreeContext(Context &context);
    ~EmbreeContext();

    /// \brief Commits the host \c Context and rebuilds the Embree scene.
    ///
    /// Rebuilding clears the current scene first. A failed sync leaves this
    /// context unusable.
    void sync();

    [[nodiscard]] Impl getImpl() const noexcept;

private:
    struct EmptyState {};

    struct TriangleSamplingStorage {
        kira::SmallVector<float, 0> areaCDF;
        kira::SmallVector<float, 0> areaPDF;
    };

    EmbreeContext(EmptyState, Context &context) noexcept;
    void reset() noexcept;

    /// Host \c Context borrowed from the owning handler.
    Context &context_;

    /// Embree device retained across scene rebuilds.
    RTCDevice device_{};

    /// Current top-level instance scene.
    RTCScene scene_{};

    /// One instanced child scene per unique mesh.
    std::vector<RTCScene> meshScenes_;

    /// Host meshes retained for the lifetime of their shared Embree buffers.
    std::vector<Ref<TriangleMesh const>> retainedMeshes_;

    /// Geometry-space views of retained mesh arrays.
    std::vector<Geometry::Impl> geometryImpls_;

    /// Triangle-sampling storage indexed by geometry.
    std::vector<TriangleSamplingStorage> triangleSampling_;

    /// Visible primitives indexed by top-level Embree instance ID.
    std::vector<Primitive::Impl> primitives_;

    /// Object-to-world transforms indexed by top-level instance ID.
    std::vector<std::array<float, 12>> transforms_;

    /// World-space normal transforms indexed by top-level instance ID.
    std::vector<std::array<float, 9>> normalTransforms_;

    /// BSDF implementations indexed by Context index.
    std::vector<BSDF::Impl> bsdfs_;

    /// EDF implementations indexed by Context index.
    std::vector<EDF::Impl> edfs_;

    /// Image textures used by BSDFs.
    EmbreeImageTexturePool imageTexturePool_;

    /// Persistent light data referenced by scene views.
    EmbreeLightSampler lightSampler_;
};

/// \brief Borrowed view of an Embree scene.
///
/// The owning EmbreeContext keeps every referenced array and Embree handle
/// valid until its next sync or destruction.
struct EmbreeContext::Impl {
    /// Current top-level Embree scene.
    RTCScene scene{};

    /// Geometry-space views.
    Geometry::Impl const *geometries{};

    /// Visible primitives indexed by Embree instance ID.
    Primitive::Impl const *primitives{};

    /// Row-major object-to-world transforms indexed by primitive.
    std::array<float, 12> const *transforms{};

    /// Inverse-transpose normal transforms indexed by primitive.
    std::array<float, 9> const *normalTransforms{};

    /// BSDF implementations indexed by Context index.
    BSDF::Impl const *bsdfs{};

    /// EDF implementations indexed by Context index.
    EDF::Impl const *edfs{};

    EmbreeImageTexturePool::Impl imageTexturePool{};

    /// Borrowed light sampler.
    LightSampler lightSampler{};

    std::uint32_t numGeometries{};  // *geometries
    std::uint32_t numPrimitives{};  // *primitives
    std::uint32_t bsdfIndexLimit{}; // *bsdfs
    std::uint32_t edfIndexLimit{};  // *edfs

public:
    /// \brief Finds the closest intersection of \p ray.
    [[nodiscard]] bool intersect(Ray const &ray, Hit &hit) const noexcept;

    /// \brief Returns whether \p ray reaches its endpoint without obstruction.
    [[nodiscard]] bool isVisible(Ray const &ray) const noexcept;

    /// \brief Returns the world-space interaction at \p hit.
    [[nodiscard]] SurfaceInteraction
    makeSurfaceInteraction(Ray const &ray, Hit const &hit) const noexcept;

    /// \brief Maps a geometry-space point through primitive \p primitiveIndex.
    [[nodiscard]] Vec3f
    transformPointToWorld(std::uint32_t primitiveIndex, Vec3f const &point) const noexcept {
        return transformPoint(transforms[primitiveIndex].data(), point);
    }

    /// \brief Maps a geometry-space normal through primitive \p primitiveIndex.
    [[nodiscard]] Vec3f
    transformNormalToWorld(std::uint32_t primitiveIndex, Vec3f const &normal) const noexcept {
        return transformVec(normalTransforms[primitiveIndex].data(), normal);
    }

    /// \brief Returns the primitive at dense \p index.
    ///
    /// \pre \p index is less than \c numPrimitives.
    [[nodiscard]] Primitive::Impl const &getPrimitive(std::uint32_t index) const noexcept {
        return primitives[index];
    }

    /// \brief Returns the geometry at dense \p index.
    ///
    /// \pre \p index is less than \c numGeometries.
    [[nodiscard]] Geometry::Impl const &getGeometry(std::uint32_t index) const noexcept {
        return geometries[index];
    }

    /// \brief Returns the BSDF at Context \p index.
    ///
    /// \pre \p index is less than \c bsdfIndexLimit.
    [[nodiscard]] BSDF::Impl const &getBSDF(std::uint32_t index) const noexcept {
        return bsdfs[index];
    }

    [[nodiscard]] EDF::Impl const &getEDF(std::uint32_t index) const noexcept {
        return edfs[index];
    }

    [[nodiscard]] LightSampler const &getLightSampler() const noexcept { return lightSampler; }
};

static_assert(std::is_standard_layout_v<EmbreeContext::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeContext::Impl>);
} // namespace flux
