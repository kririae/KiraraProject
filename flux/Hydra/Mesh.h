#pragma once

#include <pxr/imaging/hd/mesh.h>

#include <array>

#include "flux/Core/HostBuffer.h"
#include "flux/Core/Math.h"
#include "flux/Core/Object.h"

namespace flux {
class Primitive;
} // namespace flux

/// \brief A Hydra mesh that renders as triangles.
class HdFluxMesh final : public pxr::HdMesh {
public:
    explicit HdFluxMesh(pxr::SdfPath const &id);
    ~HdFluxMesh() override;

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
    /// Both buffers are null when empty. The vertex buffer has spare capacity.
    flux::Shared<flux::HostBuffer<flux::Vec3f>> points_;
    flux::Shared<flux::HostBuffer<flux::Vec3u>> triangles_;

    /// The last transform that Hydra gave, in the form of \c flux::Primitive.
    std::array<float, 12> transform_;

    /// Null when the mesh is not rendered.
    flux::Ref<flux::Primitive> primitive_;
};
