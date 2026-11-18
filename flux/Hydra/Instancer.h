#pragma once

#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderIndex.h>

#include <cstddef>
#include <vector>

#include "flux/Core/Object.h"

namespace flux {
class Context;
class Primitive;
} // namespace flux

/// \brief A Hydra instancer that gives each of its prototypes the transforms of its instances.
///
/// Each member array holds its builtin instance primvar as of the last sync, or is empty when
/// that primvar is absent or of a type that hdFLux does not read. The instancer owns no FLux
/// object.
class HdFLuxInstancer final : public pxr::HdInstancer {
public:
    HdFLuxInstancer(pxr::HdSceneDelegate *sceneDelegate, pxr::SdfPath const &id);

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    /// \brief Returns the transform of each instance of \p prototypeId, with the instances of the
    /// parent instancers.
    ///
    /// The prototype's own transform applies before each one.
    [[nodiscard]] pxr::VtMatrix4dArray
    ComputeInstanceTransforms(pxr::SdfPath const &prototypeId) const;

private:
    pxr::VtVec3dArray translations_;
    pxr::VtQuatdArray rotations_;
    pxr::VtVec3dArray scales_;
    pxr::VtMatrix4dArray transforms_;
};

/// \brief Returns the transforms that the instancer \p instancerId gives the prim \p prototypeId,
/// or one identity when \p instancerId is empty.
///
/// It syncs the instancer and its parents first.
[[nodiscard]] pxr::VtMatrix4dArray HdFLuxComputeInstanceTransforms(
    pxr::HdRenderIndex &renderIndex, pxr::SdfPath const &instancerId,
    pxr::SdfPath const &prototypeId
);

/// \brief Removes from \p context the primitives of \p primitives past the first \p count.
void HdFLuxTruncatePrimitives(
    flux::Context &context, std::vector<flux::Ref<flux::Primitive>> &primitives, std::size_t count
);
