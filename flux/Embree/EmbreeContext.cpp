#include "flux/Embree/EmbreeContext.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <utility>

#include "flux/Core/MathUtils.h"
#include "flux/Embree/EmbreeUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"
#include "kira/Assertions.h"
#include "kira/SmallVector.h"

namespace flux {
namespace {
static_assert(sizeof(Vec3f) == 3 * sizeof(float));
static_assert(sizeof(Vec3u) == 3 * sizeof(std::uint32_t));

[[nodiscard]] RTCRay makeRay(Ray const &ray) noexcept {
    RTCRay result{};
    result.org_x = ray.origin.x();
    result.org_y = ray.origin.y();
    result.org_z = ray.origin.z();
    result.tnear = ray.minDistance;
    result.dir_x = ray.direction.x();
    result.dir_y = ray.direction.y();
    result.dir_z = ray.direction.z();
    result.time = 0.0F;
    result.tfar = ray.maxDistance;
    result.mask = std::numeric_limits<unsigned int>::max();
    return result;
}

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
    geometryImpls_.clear();
    triangleSampling_.clear();
    primitives_.clear();
    transforms_.clear();
    normalTransforms_.clear();
    bsdfs_.clear();
    edfs_.clear();
    imageTexturePool_.clear();
    lightSampler_.clear();
}

void EmbreeContext::sync() try {
    reset();
    context_.commit();

    auto const contextPrimitives = context_.getObjects<Primitive>();
    auto const contextBSDFs = context_.getObjects<BSDF>();
    auto const contextEDFs = context_.getObjects<EDF>();
    auto const contextLights = context_.getObjects<Light>();
    kira::SmallVector<Ref<Primitive const>> visiblePrimitives;
    std::unordered_map<std::size_t, std::uint32_t> geometryIndexByContextId;
    geometryIndexByContextId.reserve(contextPrimitives.size());
    retainedMeshes_.reserve(contextPrimitives.size());
    geometryImpls_.reserve(contextPrimitives.size());
    triangleSampling_.reserve(contextPrimitives.size());
    primitives_.reserve(contextPrimitives.size());
    visiblePrimitives.reserve(contextPrimitives.size());
    transforms_.reserve(contextPrimitives.size());
    normalTransforms_.reserve(contextPrimitives.size());

    // Use a live implementation for unused indices. Primitive indices select
    // the live entries filled below.
    if (!contextBSDFs.empty()) {
        bsdfs_.assign(context_.getBSDFIndexLimit(), contextBSDFs.front()->getImpl());
        for (auto const &bsdf : contextBSDFs)
            bsdfs_[context_.getBSDFIndex(bsdf->getContextId())] = bsdf->getImpl();
    }
    if (!contextEDFs.empty()) {
        edfs_.assign(context_.getEDFIndexLimit(), contextEDFs.front()->getImpl());
        for (auto const &edf : contextEDFs)
            edfs_[context_.getEDFIndex(edf->getContextId())] = edf->getImpl();
    }
    imageTexturePool_.build(context_);

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
        auto impl = mesh->getImpl();
        auto &sampling = triangleSampling_.emplace_back();
        sampling.areaCDF.resize_for_overwrite(impl.numTriangles);
        sampling.areaPDF.resize_for_overwrite(impl.numTriangles);
        TriangleMesh::computeSamplingDistribution(
            impl, {sampling.areaCDF.data(), sampling.areaCDF.size()},
            {sampling.areaPDF.data(), sampling.areaPDF.size()}
        );
        impl.triangleAreaCDF = sampling.areaCDF.data();
        impl.triangleAreaPDF = sampling.areaPDF.data();
        impl.surfaceArea = sampling.areaCDF.empty() ? impl.surfaceArea : sampling.areaCDF.back();
        geometryImpls_.emplace_back(impl);
        retainedMeshes_.push_back(std::move(mesh));
        geometryIndexByContextId.emplace(contextId, index);
        return index;
    };

    // Pack visible primitives into dense Embree arrays. All arrays use the same
    // primitive order.
    for (auto const &primitive : contextPrimitives) {
        if (!primitive->isVisible())
            continue;
        if (primitives_.size() >= std::numeric_limits<std::uint32_t>::max())
            throw kira::Anyhow("EmbreeContext: primitive count exceeds Embree index limits");

        auto const geometry = primitive->getGeometry();
        auto const edf = primitive->getEDF();
        auto const geometryIndex = getOrAddGeometryIndex(geometry);
        auto const bsdf = primitive->getBSDF();
        auto bsdfIndex = Primitive::Impl::invalidBSDFIndex;
        if (bsdf)
            bsdfIndex = context_.getBSDFIndex(bsdf->getContextId());

        auto edfIndex = Primitive::Impl::invalidEDFIndex;
        if (edf)
            edfIndex = context_.getEDFIndex(edf->getContextId());

        primitives_.push_back({
            .geometryIndex = geometryIndex,
            .bsdfIndex = bsdfIndex,
            .edfIndex = edfIndex,
        });
        visiblePrimitives.push_back(primitive);
        transforms_.push_back(primitive->getTransform());
        normalTransforms_.push_back(primitive->getNormalTransform());
    }
    lightSampler_.build(contextLights, visiblePrimitives, primitives_);

    // Embree borrows retained mesh arrays until the next sync. A sync builds
    // immutable final-frame scenes, so favor traversal over build time.
    meshScenes_.reserve(retainedMeshes_.size());
    for (auto const &mesh : retainedMeshes_) {
        RTCScene childScene = rtcNewScene(device_);
        embreeCheck(device_);
        meshScenes_.push_back(childScene);
        rtcSetSceneBuildQuality(childScene, RTC_BUILD_QUALITY_HIGH);
        embreeCheck(device_);

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
    rtcSetSceneBuildQuality(scene_, RTC_BUILD_QUALITY_HIGH);
    embreeCheck(device_);
    for (std::size_t index = 0; index < primitives_.size(); ++index) {
        EmbreeGeometryHandle instance(rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_INSTANCE));
        embreeCheck(device_);
        rtcSetGeometryInstancedScene(
            instance.get(), meshScenes_[primitives_[index].getGeometryIndex()]
        );
        embreeCheck(device_);
        rtcSetGeometryTransform(
            instance.get(), 0, RTC_FORMAT_FLOAT3X4_ROW_MAJOR, transforms_[index].data()
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

EmbreeContext::Impl EmbreeContext::getImpl() const noexcept {
    return {
        .scene = scene_,
        .geometries = geometryImpls_.data(),
        .primitives = primitives_.data(),
        .transforms = transforms_.data(),
        .normalTransforms = normalTransforms_.data(),
        .bsdfs = bsdfs_.data(),
        .edfs = edfs_.data(),
        .imageTexturePool = imageTexturePool_.getImpl(),
        .lightSampler = lightSampler_.getSampler(),
        .numGeometries = static_cast<std::uint32_t>(geometryImpls_.size()),
        .numPrimitives = static_cast<std::uint32_t>(primitives_.size()),
        .bsdfIndexLimit = static_cast<std::uint32_t>(bsdfs_.size()),
        .edfIndexLimit = static_cast<std::uint32_t>(edfs_.size()),
    };
}

bool EmbreeContext::Impl::intersect(Ray const &ray, Hit &hit) const noexcept {
    if (!scene)
        return false;

    RTCRayHit rayHit{};
    rayHit.ray = makeRay(ray);
    rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    std::ranges::fill(rayHit.hit.instID, RTC_INVALID_GEOMETRY_ID);

    rtcIntersect1(scene, &rayHit);
    if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
        return false;

    hit = {
        .preliminary =
            {
                .distance = rayHit.ray.tfar,
                .coordinates = {rayHit.hit.u, rayHit.hit.v},
                .elementIndex = rayHit.hit.primID,
            },
        .geometricNormal = {rayHit.hit.Ng_x, rayHit.hit.Ng_y, rayHit.hit.Ng_z},
        .primitiveIndex = rayHit.hit.instID[0],
    };
    return true;
}

bool EmbreeContext::Impl::isVisible(Ray const &ray) const noexcept {
    if (!scene)
        return true;

    auto visibilityRay = makeRay(ray);
    rtcOccluded1(scene, &visibilityRay);
    return visibilityRay.tfar >= 0.0F;
}

SurfaceInteraction
EmbreeContext::Impl::makeSurfaceInteraction(Ray const &ray, Hit const &hit) const noexcept {
    auto const &primitive = primitives[hit.primitiveIndex];
    auto const &geometry = geometries[primitive.getGeometryIndex()];
    auto const shadingNormal =
        geometry.interpolateShadingNormal(hit.preliminary, hit.geometricNormal);
    auto const &normalTransform = normalTransforms[hit.primitiveIndex];
    return {
        .position = ray.origin + ray.direction * hit.preliminary.distance,
        .geometricNormal = transformVec(normalTransform.data(), hit.geometricNormal).normalize(),
        .shadingNormal = transformVec(normalTransform.data(), shadingNormal).normalize(),
        .uv = geometry.interpolateTexCoord(hit.preliminary),
        .primitiveIndex = hit.primitiveIndex,
        .elementIndex = hit.preliminary.elementIndex,
    };
}

} // namespace flux
