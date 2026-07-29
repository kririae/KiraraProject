#pragma once

#include <embree4/rtcore.h>

#include <array>
#include <cstdint>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "flux/Embree/EmbreeLightSampler.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Interaction.h"

namespace flux {
class Context;

/// \brief Owns the Embree scene built from one host \c Context.
///
/// The Embree scene shares immutable mesh arrays with retained meshes and owns
/// its acceleration structures, dense tables, and instance normal transforms.
/// Call \c sync from one thread. Traversal supports concurrent calls after sync.
class EmbreeContext final : private Noncopyable {
public:
    /// \brief Borrowed view of the current Embree scene.
    struct Impl;

    /// \brief Embree intersection result.
    struct Hit {
        /// Geometry-space intersection data.
        PreliminaryIntersection preliminary;

        /// Dense primitive index in this Embree scene.
        std::uint32_t primitiveIndex;
    };

    /// \brief Creates an Embree device for \p context.
    ///
    /// \throw kira::Anyhow If Embree setup fails.
    explicit EmbreeContext(Context &context);

    /// \brief Releases all Embree resources.
    ~EmbreeContext();

    /// \brief Rebuilds the Embree scene from the host \c Context.
    ///
    /// This function clears the current Embree scene before rebuilding it.
    /// Destroy this context after a failed rebuild.
    /// \throw kira::Anyhow If scene linking or Embree setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown object.
    void sync();

    /// \brief Returns a borrowed view of the current scene.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    struct EmptyState {};

    EmbreeContext(EmptyState, Context &context) noexcept;
    [[nodiscard]] static std::array<float, 9>
    makeNormalTransform(std::array<float, 12> const &transform);
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
    std::vector<TriangleMesh::Impl> meshImpls_;

    /// Visible primitives indexed by top-level Embree instance ID.
    std::vector<Primitive::Impl> primitives_;

    /// World-space normal transforms indexed by top-level instance ID.
    std::vector<std::array<float, 9>> normalTransforms_;

    /// Dense BSDF implementations referenced by \c primitives_.
    std::vector<BSDF::Impl> bsdfs_;

    /// Persistent light data and selection state.
    EmbreeLightSampler lightSampler_;
};

/// \brief Borrowed view of an Embree scene.
///
/// The owning EmbreeContext keeps every referenced array and Embree handle
/// valid until its next sync or destruction.
struct EmbreeContext::Impl {
    /// Current top-level Embree scene.
    RTCScene scene{};

    /// Geometry-space triangle mesh views.
    TriangleMesh::Impl const *geometries{};

    /// Visible primitives indexed by Embree instance ID.
    Primitive::Impl const *primitives{};

    /// Inverse-transpose normal transforms indexed by primitive.
    std::array<float, 9> const *normalTransforms{};

    /// Dense BSDF implementations.
    BSDF::Impl const *bsdfs{};

    /// Borrowed light sampler.
    LightSampler lightSampler{};

    /// Number of geometry entries.
    std::uint32_t numGeometries{};

    /// Number of primitive entries.
    std::uint32_t numPrimitives{};

    /// Number of BSDF entries.
    std::uint32_t numBSDFs{};

public:
    /// \brief Finds the closest intersection of \p ray.
    [[nodiscard]] bool intersect(Ray const &ray, Hit &hit) const noexcept;

    /// \brief Returns whether \p ray reaches its endpoint without obstruction.
    [[nodiscard]] bool isVisible(Ray const &ray) const noexcept;

    /// \brief Returns the world-space interaction at \p hit.
    [[nodiscard]] SurfaceInteraction
    makeSurfaceInteraction(Ray const &ray, Hit const &hit) const noexcept;

    /// \brief Returns the primitive at dense \p index.
    [[nodiscard]] Primitive::Impl const &getPrimitive(std::uint32_t index) const noexcept;

    /// \brief Returns the geometry at dense \p index.
    [[nodiscard]] TriangleMesh::Impl const &getGeometry(std::uint32_t index) const noexcept;

    /// \brief Returns the BSDF at dense \p index.
    [[nodiscard]] BSDF::Impl const &getBSDF(std::uint32_t index) const noexcept;

    /// \brief Returns the light sampler built with this scene.
    [[nodiscard]] LightSampler const &getLightSampler() const noexcept { return lightSampler; }
};

static_assert(std::is_standard_layout_v<EmbreeContext::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeContext::Impl>);
} // namespace flux
