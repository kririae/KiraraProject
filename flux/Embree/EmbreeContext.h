#pragma once

#include <embree4/rtcore.h>

#include <array>
#include <cstdint>
#include <vector>

#include "flux/Core/MathUtils.h"
#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "flux/Core/RayFootprint.h"
#include "flux/Embree/EmbreeImageTexturePool.h"
#include "flux/Embree/EmbreeLightSampler.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTable.h"
#include "flux/Scene/SceneTableData.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Interaction.h"
#include "kira/SmallVector.h"

namespace flux {
class Context;

/// \brief Owns the Embree scene built from one host \c Context.
///
/// The Embree scene shares immutable mesh arrays with the scene tables and owns
/// its acceleration structures, triangle sampling distributions, and instance
/// normal transforms. Call \c sync from one thread. Traversal supports
/// concurrent calls after sync.
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

    struct TriangleData {
        kira::SmallVector<float, 0> areaCDF;
        kira::SmallVector<float, 0> areaPDF;
    };

    EmbreeContext(EmptyState, Context &context) noexcept;
    void reset() noexcept;

    /// Host \c Context borrowed from the owning handler.
    Context &context_;

    /// Dense scene tables built from the host \c Context.
    SceneTableData table_;

    /// Embree device retained across scene rebuilds.
    RTCDevice device_{};

    /// Current top-level instance scene.
    RTCScene scene_{};

    /// One instanced child scene per unique mesh.
    std::vector<RTCScene> meshScenes_;

    /// Geometry-space views of the mesh arrays the tables retain.
    std::vector<Geometry::Impl> geometryImpls_;

    /// Per-triangle data for each geometry.
    std::vector<TriangleData> triangleData_;

    /// World-space normal transforms indexed by dense primitive index.
    std::vector<std::array<float, 9>> normalTransforms_;

    /// Image textures used by BSDFs.
    EmbreeImageTexturePool imageTexturePool_;

    EmbreeLightSampler lightSampler_;
};

/// \brief Embree scene used during rendering.
///
/// EmbreeContext keeps the referenced memory and handles valid until its next
/// sync.
struct EmbreeContext::Impl {
    /// Current top-level Embree scene.
    RTCScene scene{};

    /// Dense scene tables in host memory.
    SceneTable table{};

    /// Object-to-world transforms indexed by dense primitive index.
    std::array<float, 12> const *transforms{};

    /// World-space normal transforms indexed by dense primitive index.
    std::array<float, 9> const *normalTransforms{};

    EmbreeImageTexturePool::Impl imageTexturePool{};

    EmbreeLightSampler::Impl lightSampler{};

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

    /// \brief Returns the PDF of sampling \p isect from \p ctx.
    ///
    /// The PDF includes light selection.
    [[nodiscard]] float
    pdfDirectLight(LightSamplingContext const &ctx, SurfaceInteraction const &isect) const noexcept;
};

static_assert(std::is_standard_layout_v<EmbreeContext::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeContext::Impl>);
} // namespace flux
