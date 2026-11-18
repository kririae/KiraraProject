#pragma once

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshTopology.h>

#include <vector>

#include "flux/Core/HostBuffer.h"
#include "flux/Core/Math.h"
#include "flux/Core/Object.h"

namespace flux {
class Primitive;
class TriangleMesh;
} // namespace flux

/// \brief A Hydra mesh that renders as triangles, once per instance.
///
/// After a sync that throws nothing, \c primitives_ holds one primitive per element of
/// \c instanceTransforms_ when \c geometry_ is non-null, and none otherwise. Primitive \c i has
/// the geometry \c geometry_, the transform <tt>transform_ * instanceTransforms_[i]</tt>, and the
/// visibility and material of the mesh.
class HdFLuxMesh final : public pxr::HdMesh {
public:
    explicit HdFLuxMesh(pxr::SdfPath const &id);
    ~HdFLuxMesh() override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits, pxr::TfToken const &reprToken
    ) override;

    void Finalize(pxr::HdRenderParam *renderParam) override;

protected:
    void _InitRepr(pxr::TfToken const &reprToken, pxr::HdDirtyBits *dirtyBits) override;

    pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override;

private:
    /// Null when empty. The buffer has spare capacity.
    flux::Shared<flux::HostBuffer<flux::Vec3f>> points_;
    pxr::HdMeshTopology topology_;

    /// Null when empty.
    flux::Shared<flux::HostBuffer<flux::Vec3u>> triangles_;
    pxr::VtIntArray trianglePrimitiveParams_;

    flux::Ref<flux::TriangleMesh const> geometry_;

    pxr::GfMatrix4d transform_{1.0};

    pxr::VtMatrix4dArray instanceTransforms_;

    std::vector<flux::Ref<flux::Primitive>> primitives_;
};
