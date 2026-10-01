#include "flux/Embree/EmbreeContext.h"

#include <Eigen/Core>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

#include "flux/Core/Logging.h"
#include "flux/Core/MathUtils.h"
#include "flux/Embree/EmbreeUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/LightSamplingImpl.h"
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

/// \brief Returns the inverse-transpose of the linear part of \p transform.
///
/// \throw kira::Anyhow If \p transform is singular.
[[nodiscard]] std::array<float, 9> makeNormalTransform(std::array<float, 12> const &transform) {
    using AffineTransform = Eigen::Matrix<float, 3, 4, Eigen::RowMajor>;
    using NormalMatrix = Eigen::Matrix<float, 3, 3, Eigen::RowMajor>;

    Eigen::Map<AffineTransform const> objectToWorld(transform.data());
    Eigen::Matrix3f const linear = objectToWorld.leftCols<3>();
    if (linear.determinant() == 0.0F)
        throw kira::Anyhow("EmbreeContext: transform is singular");

    std::array<float, 9> result{};
    Eigen::Map<NormalMatrix> normalTransform(result.data());
    normalTransform = linear.inverse().transpose();
    return result;
}
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
    for (auto const &geometry : geometries_)
        if (geometry.scene)
            rtcReleaseScene(geometry.scene);
    geometries_.clear();
    geometryImpls_.clear();
    table_.clear();
    normalTransforms_.clear();
    imageTexturePool_.clear();
    lightSampler_.clear();
}

void EmbreeContext::sync() try {
    reset();
    context_.commit();

    table_.build(context_);
    imageTexturePool_.build(context_);

    // Size both arrays once, so no later step moves an entry. Holes keep their
    // default entry and Impl.
    geometries_.resize(table_.objects.meshes.size());
    geometryImpls_.resize(table_.objects.meshes.size());
    for (std::size_t index = 0; index < geometries_.size(); ++index) {
        auto const &mesh = table_.objects.meshes[index];
        if (!mesh)
            continue;

        // Hold the arrays that the child scene and the Impl read.
        auto &entry = geometries_[index];
        entry.data = mesh->getData();

        // Build the triangle selection distribution.
        auto impl = TriangleMesh::makeImpl(entry.data, mesh->getSurfaceArea());
        entry.areaCDF.resize_for_overwrite(impl.numTriangles);
        entry.areaPDF.resize_for_overwrite(impl.numTriangles);
        TriangleMesh::computeSamplingDistribution(
            impl, {entry.areaCDF.data(), entry.areaCDF.size()},
            {entry.areaPDF.data(), entry.areaPDF.size()}
        );

        // Publish the Impl with the distribution.
        impl.triangleAreaCDF = entry.areaCDF.data();
        impl.triangleAreaPDF = entry.areaPDF.data();
        impl.surfaceArea = entry.areaCDF.empty() ? impl.surfaceArea : entry.areaCDF.back();
        geometryImpls_[index] = impl;
    }

    normalTransforms_.reserve(table_.primitives.size());
    for (auto const &transform : table_.transforms)
        normalTransforms_.push_back(makeNormalTransform(transform));

    // Embree borrows the entry arrays until the next sync. The scenes are
    // immutable, so they favor traversal over build time.
    for (std::size_t index = 0; index < geometries_.size(); ++index) {
        auto &entry = geometries_[index];
        if (!entry.data.vertices)
            continue;

        RTCScene childScene = rtcNewScene(device_);
        embreeCheck(device_);
        entry.scene = childScene;
        rtcSetSceneBuildQuality(childScene, RTC_BUILD_QUALITY_HIGH);
        embreeCheck(device_);

        EmbreeGeometryHandle geometry(rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_TRIANGLE));
        embreeCheck(device_);
        auto const &vertices = *entry.data.vertices;
        auto const &triangles = *entry.data.triangles;

        // Embree reads 16 bytes from the last RTC_FORMAT_FLOAT3 vertex.
        if (vertices.capacity() <= vertices.size())
            throw kira::Anyhow(
                "EmbreeContext: the vertex buffer of mesh {} has no spare capacity",
                table_.objects.meshes[index]->getContextId()
            );

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
    for (std::size_t index = 0; index < table_.primitives.size(); ++index) {
        EmbreeGeometryHandle instance(rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_INSTANCE));
        embreeCheck(device_);
        rtcSetGeometryInstancedScene(
            instance.get(), geometries_[table_.primitives[index].getGeometryIndex()].scene
        );
        embreeCheck(device_);
        rtcSetGeometryTransform(
            instance.get(), 0, RTC_FORMAT_FLOAT3X4_ROW_MAJOR, table_.transforms[index].data()
        );
        embreeCheck(device_);
        rtcCommitGeometry(instance.get());
        embreeCheck(device_);
        rtcAttachGeometryByID(scene_, instance.get(), static_cast<unsigned int>(index));
        embreeCheck(device_);
    }
    rtcCommitScene(scene_);
    embreeCheck(device_);

    auto sceneRadius = 1.0F;
    if (!table_.primitives.empty()) {
        RTCBounds bounds{};
        rtcGetSceneBounds(scene_, &bounds);
        embreeCheck(device_);
        sceneRadius =
            Vec3f{
                bounds.upper_x - bounds.lower_x,
                bounds.upper_y - bounds.lower_y,
                bounds.upper_z - bounds.lower_z,
            }
                .norm() *
            0.5F;
    }
    lightSampler_.build(table_, context_, imageTexturePool_.getImpl(), sceneRadius);
    LogDebug(
        "EmbreeContext: built {} geometries and {} visible primitives",
        std::ranges::count_if(
            table_.objects.meshes, [](auto const &mesh) { return static_cast<bool>(mesh); }
        ),
        table_.primitives.size()
    );
} catch (...) {
    reset();
    throw;
}

EmbreeContext::Impl EmbreeContext::getImpl() const noexcept {
    return {
        .scene = scene_,
        .table =
            {
                .geometries = geometryImpls_.data(),
                .primitives = table_.primitives.data(),
                .bsdfs = table_.bsdfs.data(),
                .edfs = table_.edfs.data(),
            },
        .transforms = table_.transforms.data(),
        .normalTransforms = normalTransforms_.data(),
        .imageTexturePool = imageTexturePool_.getImpl(),
        .lightSampler = lightSampler_.getImpl(),
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
    auto const &primitive = table.getPrimitive(hit.primitiveIndex);
    auto const &geometry = table.getGeometry(primitive.getGeometryIndex());
    auto const geometryInteraction = geometry.computeInteraction(hit.preliminary);
    auto const shadingNormal =
        geometry.interpolateShadingNormal(hit.preliminary, hit.geometricNormal);
    auto const &normalTransform = normalTransforms[hit.primitiveIndex];
    // A parameterization gradient maps a position offset to a uv offset, so it is a covector
    // and transforms by the inverse transpose, exactly like a normal.
    auto const uvGradU = transformVec(normalTransform.data(), geometryInteraction.uvGradU);
    auto const uvGradV = transformVec(normalTransform.data(), geometryInteraction.uvGradV);
    return {
        .position = ray.origin + ray.direction * hit.preliminary.distance,
        .geometricNormal = transformVec(normalTransform.data(), hit.geometricNormal).normalize(),
        .shadingNormal = transformVec(normalTransform.data(), shadingNormal).normalize(),
        .uv = geometryInteraction.uv,
        .uvGradU = uvGradU,
        .uvGradV = uvGradV,
        .primitiveIndex = hit.primitiveIndex,
        .elementIndex = hit.preliminary.elementIndex,
    };
}

float EmbreeContext::Impl::pdfDirectLight(
    LightSamplingContext const &ctx, SurfaceInteraction const &isect
) const noexcept {
    return flux::pdfDirectLight(*this, ctx, isect);
}

} // namespace flux
