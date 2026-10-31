#include "flux/Hydra/Mesh.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <numeric>
#include <stdexcept>

#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/Transform.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
static_assert(sizeof(GfVec3f) == sizeof(flux::Vec3f));

[[nodiscard]] flux::Shared<flux::HostBuffer<flux::Vec3f>> copyPoints(VtVec3fArray const &points) {
    if (points.empty())
        return nullptr;

    auto buffer = std::make_shared<flux::HostBuffer<flux::Vec3f>>();
    buffer->resize(points.size(), points.size() + 1);
    std::memcpy(buffer->data(), points.cdata(), points.size() * sizeof(flux::Vec3f));
    return buffer;
}

[[nodiscard]] flux::Shared<flux::HostBuffer<flux::Vec3u>>
triangulate(HdMeshTopology const &topology, SdfPath const &id) {
    auto const &counts = topology.GetFaceVertexCounts();
    if (std::reduce(counts.begin(), counts.end(), std::size_t{}) !=
        topology.GetFaceVertexIndices().size())
        throw std::invalid_argument("The face vertex counts do not match the face vertex indices");

    VtVec3iArray indices;
    VtIntArray primitiveParams;
    HdMeshUtil{&topology, id}.ComputeTriangleIndices(&indices, &primitiveParams);
    if (indices.empty())
        return nullptr;

    auto buffer = std::make_shared<flux::HostBuffer<flux::Vec3u>>();
    buffer->resize(indices.size());
    std::ranges::transform(indices, buffer->data(), [](GfVec3i const &triangle) {
        return flux::Vec3u{
            static_cast<std::uint32_t>(triangle[0]), static_cast<std::uint32_t>(triangle[1]),
            static_cast<std::uint32_t>(triangle[2])
        };
    });
    return buffer;
}

void removePrimitive(flux::Context &context, flux::Ref<flux::Primitive> &primitive) {
    if (!primitive)
        return;

    context.remove(primitive->getContextId());
    primitive.reset();
}
} // namespace

HdFluxMesh::HdFluxMesh(SdfPath const &id)
    : HdMesh(id), transform_(HdFluxToTransform(GfMatrix4d{1.0})) {}

HdFluxMesh::~HdFluxMesh() = default;

HdDirtyBits HdFluxMesh::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::Clean | HdChangeTracker::InitRepr | HdChangeTracker::DirtyPoints |
           HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyTransform |
           HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyInstancer;
}

void HdFluxMesh::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits,
    TfToken const &
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    _UpdateInstancer(sceneDelegate, dirtyBits);

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id))
        transform_ = HdFluxToTransform(sceneDelegate->GetTransform(id));
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
        _UpdateVisibility(sceneDelegate, dirtyBits);

    try {
        auto const context = param.GetContext();

        auto const pointsDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points);
        auto const topologyDirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);

        if (pointsDirty)
            points_ =
                copyPoints(sceneDelegate->Get(id, HdTokens->points).GetWithDefault<VtVec3fArray>());
        if (topologyDirty)
            triangles_ = triangulate(GetMeshTopology(sceneDelegate), id);

        auto const instanced = !GetInstancerId().IsEmpty();
        if (instanced && HdChangeTracker::IsInstancerDirty(*dirtyBits, id))
            TF_WARN("%s: instancing is not supported, so the mesh is not rendered", id.GetText());

        if (instanced || !points_ || !triangles_) {
            removePrimitive(*context, primitive_);
        } else if (pointsDirty || topologyDirty || !primitive_) {
            flux::TriangleMesh::Data data;
            data.vertices = points_;
            data.triangles = triangles_;

            flux::TriangleMesh::checkIndices(data);
            auto const mesh = context->create<flux::TriangleMesh>(std::move(data));

            if (primitive_) {
                primitive_->setGeometry(mesh);
            } else {
                primitive_ = context->create<flux::Primitive>(mesh);
                primitive_->setBSDF(param.GetFallbackBSDF());
            }
        }

        if (primitive_) {
            primitive_->setTransform(transform_);
            primitive_->setVisible(IsVisible());
        }
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdFluxMesh::Finalize(HdRenderParam *renderParam) {
    removePrimitive(*static_cast<HdFluxRenderParam *>(renderParam)->GetContext(), primitive_);
}

void HdFluxMesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *) {
    auto const found = std::ranges::find_if(_reprs, _ReprComparator(reprToken));
    if (found == _reprs.end())
        _reprs.emplace_back(reprToken, HdReprSharedPtr());
}

HdDirtyBits HdFluxMesh::_PropagateDirtyBits(HdDirtyBits bits) const { return bits; }
