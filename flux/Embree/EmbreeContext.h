#pragma once

#include <embree4/rtcore.h>

#include <array>
#include <cstdint>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
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

    /// \brief Finds the closest intersection of \p ray.
    ///
    /// Concurrent calls are safe after a successful sync.
    [[nodiscard]] bool intersect(Ray const &ray, Hit &hit) const noexcept;

    /// \brief Returns the world-space interaction at \p hit.
    [[nodiscard]] SurfaceInteraction
    makeSurfaceInteraction(Ray const &ray, Hit const &hit) const noexcept;

    /// \brief Returns the primitive at \p index.
    [[nodiscard]] Primitive::Impl const &getPrimitive(std::uint32_t index) const noexcept;

    /// \brief Returns the BSDF at \p index.
    [[nodiscard]] BSDF::Impl const &getBSDF(std::uint32_t index) const noexcept;

private:
    struct EmptyState {};

    /// \brief Applies an inverse-transpose instance transform to a normal.
    struct NormalTransform {
        std::array<float, 9> values{};

        [[nodiscard]] Vec3f apply(Vec3f const &normal) const noexcept;
    };

    EmbreeContext(EmptyState, Context &context) noexcept;
    [[nodiscard]] static NormalTransform
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
    std::vector<NormalTransform> normalTransforms_;

    /// Dense BSDF implementations referenced by \c primitives_.
    std::vector<BSDF::Impl> bsdfs_;
};
} // namespace flux
