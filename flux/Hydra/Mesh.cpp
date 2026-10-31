#include "flux/Hydra/Mesh.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>

#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/Transform.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
static_assert(sizeof(GfVec3f) == sizeof(flux::Vec3f));

[[nodiscard]] flux::Shared<flux::HostBuffer<flux::Vec3f>> copyVectors(VtVec3fArray const &vectors) {
    if (vectors.empty())
        return nullptr;

    auto buffer = std::make_shared<flux::HostBuffer<flux::Vec3f>>();
    buffer->resize(vectors.size(), vectors.size() + 1);
    std::memcpy(buffer->data(), vectors.cdata(), vectors.size() * sizeof(flux::Vec3f));
    return buffer;
}

[[nodiscard]] flux::Shared<flux::HostBuffer<flux::Vec3u>> toBuffer(VtVec3iArray const &indices) {
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

struct Triangulation {
    flux::Shared<flux::HostBuffer<flux::Vec3u>> triangles;
    VtIntArray primitiveParams;
};

[[nodiscard]] Triangulation triangulate(HdMeshTopology const &topology, SdfPath const &id) {
    auto const &counts = topology.GetFaceVertexCounts();
    if (std::reduce(counts.begin(), counts.end(), std::size_t{}) !=
        topology.GetFaceVertexIndices().size())
        throw std::invalid_argument("The face vertex counts do not match the face vertex indices");

    VtVec3iArray indices;
    Triangulation triangulation;
    HdMeshUtil{&topology, id}.ComputeTriangleIndices(&indices, &triangulation.primitiveParams);
    triangulation.triangles = toBuffer(indices);
    return triangulation;
}

[[nodiscard]] std::optional<HdInterpolation>
findNormalsInterpolation(HdSceneDelegate &sceneDelegate, SdfPath const &id) {
    static constexpr std::array interpolations{
        HdInterpolationConstant, HdInterpolationUniform,     HdInterpolationVarying,
        HdInterpolationVertex,   HdInterpolationFaceVarying,
    };

    auto const found = std::ranges::find_if(interpolations, [&](HdInterpolation interpolation) {
        auto const descriptors = sceneDelegate.GetPrimvarDescriptors(id, interpolation);
        return std::ranges::any_of(descriptors, [](HdPrimvarDescriptor const &descriptor) {
            return descriptor.name == HdTokens->normals;
        });
    });
    if (found == interpolations.end())
        return std::nullopt;
    return *found;
}

void mapNormals(
    flux::TriangleMesh::Data &data, VtValue const &value, HdInterpolation interpolation,
    HdMeshTopology const &topology, VtIntArray const &primitiveParams, SdfPath const &id
) {
    if (!value.IsHolding<VtVec3fArray>())
        throw std::invalid_argument("The normals are not a float3 array");
    auto const &normals = value.UncheckedGet<VtVec3fArray>();

    std::size_t count{};
    VtVec3iArray indices;
    switch (interpolation) {
    case HdInterpolationConstant:
        count = 1;
        indices = VtVec3iArray(data.triangles->size(), GfVec3i{0});
        break;
    case HdInterpolationUniform:
        count = static_cast<std::size_t>(topology.GetNumFaces());
        indices.resize(primitiveParams.size());
        std::ranges::transform(primitiveParams, indices.begin(), [](int param) {
            return GfVec3i{HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(param)};
        });
        break;
    case HdInterpolationFaceVarying: {
        auto const &faceVertexIndices = topology.GetFaceVertexIndices();
        count = faceVertexIndices.size();

        VtIntArray sequence(faceVertexIndices.size());
        std::iota(sequence.begin(), sequence.end(), 0);
        HdMeshTopology const sequential{
            topology.GetScheme(),           topology.GetOrientation(),
            topology.GetFaceVertexCounts(), sequence,
            topology.GetHoleIndices(),
        };
        VtIntArray unusedParams;
        HdMeshUtil{&sequential, id}.ComputeTriangleIndices(&indices, &unusedParams);
        break;
    }
    case HdInterpolationVertex:
    case HdInterpolationVarying:
    default: count = data.vertices->size(); break;
    }

    if (normals.size() != count)
        throw std::invalid_argument(
            "The normals have " + std::to_string(normals.size()) +
            " values, but their interpolation needs " + std::to_string(count)
        );

    data.normals = copyVectors(normals);
    data.normalIndices = toBuffer(indices);
}

[[nodiscard]] flux::TriangleMesh::Data makeData(
    HdSceneDelegate &sceneDelegate, SdfPath const &id, HdMeshTopology const &topology,
    flux::Shared<flux::HostBuffer<flux::Vec3f>> const &points,
    flux::Shared<flux::HostBuffer<flux::Vec3u>> const &triangles, VtIntArray const &primitiveParams
) {
    flux::TriangleMesh::Data data;
    data.vertices = points;
    data.triangles = triangles;

    auto const interpolation = findNormalsInterpolation(sceneDelegate, id);
    if (interpolation)
        mapNormals(
            data, sceneDelegate.Get(id, HdTokens->normals), *interpolation, topology,
            primitiveParams, id
        );

    flux::TriangleMesh::checkIndices(data);

    auto const scheme = topology.GetScheme();
    if (!interpolation && scheme != PxOsdOpenSubdivTokens->none &&
        scheme != PxOsdOpenSubdivTokens->bilinear)
        data.normals =
            flux::TriangleMesh::computeVertexNormals(data.vertices->span(), data.triangles->span());
    return data;
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
           HdChangeTracker::DirtyNormals | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility |
           HdChangeTracker::DirtyInstancer;
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
        auto const normalsDirty =
            HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals);

        if (pointsDirty)
            points_ = copyVectors(
                sceneDelegate->Get(id, HdTokens->points).GetWithDefault<VtVec3fArray>()
            );
        if (topologyDirty) {
            auto topology = GetMeshTopology(sceneDelegate);
            auto triangulation = triangulate(topology, id);
            topology_ = std::move(topology);
            triangles_ = std::move(triangulation.triangles);
            trianglePrimitiveParams_ = std::move(triangulation.primitiveParams);
        }

        auto const instanced = !GetInstancerId().IsEmpty();
        if (instanced && HdChangeTracker::IsInstancerDirty(*dirtyBits, id))
            TF_WARN("%s: instancing is not supported, so the mesh is not rendered", id.GetText());

        if (instanced || !points_ || !triangles_) {
            removePrimitive(*context, primitive_);
        } else if (pointsDirty || topologyDirty || normalsDirty || !primitive_) {
            auto data = makeData(
                *sceneDelegate, id, topology_, points_, triangles_, trianglePrimitiveParams_
            );
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
