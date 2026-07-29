#include "flux/Embree/EmbreeContext.h"

#include <Eigen/Core>
#include <Eigen/LU>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>

#include "flux/Embree/EmbreeUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"
#include "kira/Anyhow.h"
#include "kira/Assertions.h"
#include "kira/SmallVector.h"

namespace flux {
namespace {
static_assert(sizeof(Vec3f) == 3 * sizeof(float));
static_assert(sizeof(Vec3u) == 3 * sizeof(std::uint32_t));

class EmbreeGeometryHandle final : private Noncopyable {
public:
    explicit EmbreeGeometryHandle(RTCGeometry geometry) noexcept : geometry_(geometry) {}
    ~EmbreeGeometryHandle() {
        if (geometry_)
            rtcReleaseGeometry(geometry_);
    }

    [[nodiscard]] RTCGeometry get() const noexcept { return geometry_; }

private:
    RTCGeometry geometry_;
};
} // namespace

EmbreeContext::EmbreeContext(EmptyState, Context &context) noexcept : context_(context) {}

EmbreeContext::EmbreeContext(Context &context) : EmbreeContext(EmptyState{}, context) {
    device_ = rtcNewDevice(nullptr);
    embreeCheck(device_);
    if (!device_)
        throw kira::Anyhow("EmbreeContext: failed to create an Embree device");
}

EmbreeContext::~EmbreeContext() {
    reset();
    if (device_)
        rtcReleaseDevice(device_);
}

void EmbreeContext::reset() noexcept {
    if (scene_)
        rtcReleaseScene(scene_);
    scene_ = nullptr;
    for (RTCScene const scene : meshScenes_)
        rtcReleaseScene(scene);
    meshScenes_.clear();
    retainedMeshes_.clear();
    meshImpls_.clear();
    primitives_.clear();
    normalTransforms_.clear();
    bsdfs_.clear();
}

void EmbreeContext::sync() try {
    reset();
    context_.commit();

    auto const contextPrimitives = context_.getObjects<Primitive>();
    auto const contextBSDFs = context_.getObjects<BSDF>();
    std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
    std::unordered_map<std::size_t, std::uint32_t> bsdfIndexByContextId;
    geometryIndexByContextId.reserve(contextPrimitives.size());
    bsdfIndexByContextId.reserve(contextBSDFs.size());
    retainedMeshes_.reserve(contextPrimitives.size());
    meshImpls_.reserve(contextPrimitives.size());
    primitives_.reserve(contextPrimitives.size());
    normalTransforms_.reserve(contextPrimitives.size());
    bsdfs_.reserve(contextBSDFs.size());

    if (contextBSDFs.size() > Primitive::Impl::invalidBSDFIndex)
        throw kira::Anyhow("EmbreeContext: BSDF count exceeds Embree index limits");

    // Context IDs may contain gaps. Assign each BSDF a dense Embree scene index.
    for (auto const &bsdf : contextBSDFs) {
        auto const index = static_cast<std::uint32_t>(bsdfs_.size());
        bsdfIndexByContextId.emplace(bsdf->getContextId(), index);
        bsdfs_.push_back(bsdf->getImpl());
    }

    auto const getOrAddGeometryIndex = [&](Ref<Geometry const> const &geometry) {
        auto const contextId = geometry->getContextId();
        if (auto const iterator = geometryIndexByContextId.find(contextId);
            iterator != geometryIndexByContextId.end())
            return iterator->second;

        if (retainedMeshes_.size() >= std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("EmbreeContext: geometry count exceeds Embree index limits");

        auto mesh = geometry.dynamicCast<TriangleMesh const>();
        if (!mesh || geometry->getType() != GeometryType::TriangleMesh)
            throw kira::Anyhow("EmbreeContext: unsupported geometry implementation");

        auto const index = static_cast<std::uint32_t>(retainedMeshes_.size());
        meshImpls_.push_back(mesh->getImpl());
        retainedMeshes_.push_back(std::move(mesh));
        geometryIndexByContextId.emplace(contextId, index);
        return index;
    };

    kira::SmallVector<std::array<float, 12>> instanceTransforms;
    instanceTransforms.reserve(contextPrimitives.size());

    // Pack visible primitives into dense Embree arrays. All arrays use the same
    // primitive order.
    for (auto const &primitive : contextPrimitives) {
        if (!primitive->isVisible())
            continue;
        if (primitives_.size() >= std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("EmbreeContext: primitive count exceeds Embree index limits");

        auto const geometry = primitive->getGeometry();
        auto const geometryIndex = getOrAddGeometryIndex(geometry);
        auto const bsdf = primitive->getBSDF();
        auto bsdfIndex = Primitive::Impl::invalidBSDFIndex;
        if (bsdf) {
            auto const iterator = bsdfIndexByContextId.find(bsdf->getContextId());
            KIRA_ASSERT(
                iterator != bsdfIndexByContextId.end(),
                "Linked BSDF is missing from the Embree scene"
            );
            bsdfIndex = iterator->second;
        }

        primitives_.push_back({
            .geometryIndex = geometryIndex,
            .bsdfIndex = bsdfIndex,
        });
        instanceTransforms.push_back(primitive->getTransform());
        normalTransforms_.push_back(makeNormalTransform(primitive->getTransform()));
    }

    // Embree reads each retained mesh array directly during traversal. Keep
    // these arrays unchanged until sync replaces the scene. Mutable geometry
    // must publish replacement storage or use Embree-owned buffers.
    meshScenes_.reserve(retainedMeshes_.size());
    for (auto const &mesh : retainedMeshes_) {
        RTCScene childScene = rtcNewScene(device_);
        embreeCheck(device_);
        meshScenes_.push_back(childScene);

        EmbreeGeometryHandle geometry(rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_TRIANGLE));
        embreeCheck(device_);
        auto const vertices = mesh->getVertices();
        auto const triangles = mesh->getTriangles();

        rtcSetSharedGeometryBuffer(
            geometry.get(), RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, vertices.data(), 0,
            sizeof(Vec3f), vertices.size()
        );
        embreeCheck(device_);
        rtcSetSharedGeometryBuffer(
            geometry.get(), RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, triangles.data(), 0,
            sizeof(Vec3u), triangles.size()
        );
        embreeCheck(device_);
        rtcCommitGeometry(geometry.get());
        embreeCheck(device_);
        rtcAttachGeometryByID(childScene, geometry.get(), 0);
        embreeCheck(device_);
        rtcCommitScene(childScene);
        embreeCheck(device_);
    }

    // Instance each mesh scene into the top-level scene. Explicit geometry IDs
    // make Embree hit IDs identical to primitive-array indices.
    scene_ = rtcNewScene(device_);
    embreeCheck(device_);
    for (std::size_t index = 0; index < primitives_.size(); ++index) {
        EmbreeGeometryHandle instance(rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_INSTANCE));
        embreeCheck(device_);
        rtcSetGeometryInstancedScene(
            instance.get(), meshScenes_[primitives_[index].getGeometryIndex()]
        );
        embreeCheck(device_);
        rtcSetGeometryTransform(
            instance.get(), 0, RTC_FORMAT_FLOAT3X4_ROW_MAJOR, instanceTransforms[index].data()
        );
        embreeCheck(device_);
        rtcCommitGeometry(instance.get());
        embreeCheck(device_);
        rtcAttachGeometryByID(scene_, instance.get(), static_cast<unsigned int>(index));
        embreeCheck(device_);
    }
    rtcCommitScene(scene_);
    embreeCheck(device_);
} catch (...) {
    reset();
    throw;
}

bool EmbreeContext::intersect(Ray const &ray, Hit &hit) const noexcept {
    RTCRayHit rayHit{};
    rayHit.ray.org_x = ray.origin.x();
    rayHit.ray.org_y = ray.origin.y();
    rayHit.ray.org_z = ray.origin.z();
    rayHit.ray.tnear = ray.minDistance;
    rayHit.ray.dir_x = ray.direction.x();
    rayHit.ray.dir_y = ray.direction.y();
    rayHit.ray.dir_z = ray.direction.z();
    rayHit.ray.time = 0.0F;
    rayHit.ray.tfar = ray.maxDistance;
    rayHit.ray.mask = std::numeric_limits<unsigned int>::max();
    rayHit.ray.id = 0;
    rayHit.ray.flags = 0;
    rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    std::ranges::fill(rayHit.hit.instID, RTC_INVALID_GEOMETRY_ID);

    RTCIntersectArguments arguments;
    rtcInitIntersectArguments(&arguments);
    rtcIntersect1(scene_, &rayHit, &arguments);
    if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
        return false;

    hit = {
        .preliminary =
            {
                .distance = rayHit.ray.tfar,
                .coordinates = {rayHit.hit.u, rayHit.hit.v},
                .elementIndex = rayHit.hit.primID,
            },
        .primitiveIndex = rayHit.hit.instID[0],
    };
    return true;
}

SurfaceInteraction
EmbreeContext::makeSurfaceInteraction(Ray const &ray, Hit const &hit) const noexcept {
    auto const &primitive = primitives_[hit.primitiveIndex];
    auto const geometryInteraction =
        meshImpls_[primitive.getGeometryIndex()].computeInteraction(hit.preliminary);
    auto const &normalTransform = normalTransforms_[hit.primitiveIndex];
    return {
        .position = ray.origin + ray.direction * hit.preliminary.distance,
        .geometricNormal = normalTransform.apply(geometryInteraction.geometricNormal).normalize(),
        .shadingNormal = normalTransform.apply(geometryInteraction.shadingNormal).normalize(),
        .uv = geometryInteraction.uv,
        .primitiveIndex = hit.primitiveIndex,
        .elementIndex = geometryInteraction.elementIndex,
        .distance = hit.preliminary.distance,
    };
}

Primitive::Impl const &EmbreeContext::getPrimitive(std::uint32_t index) const noexcept {
    return primitives_[index];
}

BSDF::Impl const &EmbreeContext::getBSDF(std::uint32_t index) const noexcept {
    return bsdfs_[index];
}

Vec3f EmbreeContext::NormalTransform::apply(Vec3f const &normal) const noexcept {
    return {
        values[0] * normal.x() + values[1] * normal.y() + values[2] * normal.z(),
        values[3] * normal.x() + values[4] * normal.y() + values[5] * normal.z(),
        values[6] * normal.x() + values[7] * normal.y() + values[8] * normal.z(),
    };
}

EmbreeContext::NormalTransform
EmbreeContext::makeNormalTransform(std::array<float, 12> const &transform) {
    using AffineTransform = Eigen::Matrix<float, 3, 4, Eigen::RowMajor>;
    using NormalMatrix = Eigen::Matrix<float, 3, 3, Eigen::RowMajor>;

    Eigen::Map<AffineTransform const> objectToWorld(transform.data());
    Eigen::Matrix3f const linear = objectToWorld.leftCols<3>();
    if (!linear.allFinite())
        throw kira::Anyhow("EmbreeContext: primitive transform is not finite");
    auto const decomposition = linear.fullPivLu();
    if (!decomposition.isInvertible())
        throw kira::Anyhow("EmbreeContext: primitive transform is singular");

    NormalTransform result;
    Eigen::Map<NormalMatrix> normalMatrix(result.values.data());
    normalMatrix = decomposition.inverse().transpose();
    if (!normalMatrix.allFinite())
        throw kira::Anyhow("EmbreeContext: primitive normal transform is not finite");
    return result;
}
} // namespace flux
