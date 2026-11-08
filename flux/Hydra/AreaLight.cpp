#include "flux/Hydra/AreaLight.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <numbers>
#include <span>
#include <variant>
#include <vector>

#include "flux/Core/HostBuffer.h"
#include "flux/Core/MathUtils.h"
#include "flux/Hydra/LightEmission.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/Transform.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/EDF.h"
#include "kira/Properties.h"
#include "kira/Utils.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
constexpr std::uint32_t numSegments{64};
constexpr std::uint32_t numRings{32};

[[nodiscard]] flux::Ref<flux::TriangleMesh> createTriangleMesh(
    flux::Context &context, std::span<flux::Vec3f const> points,
    std::span<flux::Vec3u const> triangles
) {
    auto vertices = std::make_shared<flux::HostBuffer<flux::Vec3f>>();
    vertices->resize(points.size(), points.size() + 1);
    std::ranges::copy(points, vertices->data());

    auto indices = std::make_shared<flux::HostBuffer<flux::Vec3u>>();
    indices->resize(triangles.size());
    std::ranges::copy(triangles, indices->data());

    flux::TriangleMesh::Data data;
    data.vertices = std::move(vertices);
    data.triangles = std::move(indices);
    return context.create<flux::TriangleMesh>(std::move(data));
}

[[nodiscard]] std::uint32_t nextSegment(std::uint32_t segment) {
    return (segment + 1) % numSegments;
}

[[nodiscard]] flux::Vec2f pointOnUnitCircle(std::uint32_t segment) {
    auto const angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) /
                       static_cast<float>(numSegments);
    return {std::cos(angle), std::sin(angle)};
}

/// The geometric normal is -Z.
[[nodiscard]] flux::Ref<flux::TriangleMesh>
createRectangle(flux::Context &context, float width, float height) {
    auto const halfWidth = width * 0.5F;
    auto const halfHeight = height * 0.5F;

    std::array const points{
        flux::Vec3f{-halfWidth, -halfHeight, 0.0F},
        flux::Vec3f{-halfWidth, halfHeight, 0.0F},
        flux::Vec3f{halfWidth, halfHeight, 0.0F},
        flux::Vec3f{halfWidth, -halfHeight, 0.0F},
    };
    std::array const triangles{flux::Vec3u{0, 1, 2}, flux::Vec3u{0, 2, 3}};
    return createTriangleMesh(context, points, triangles);
}

/// The geometric normal is -Z.
[[nodiscard]] flux::Ref<flux::TriangleMesh> createDisk(flux::Context &context, float radius) {
    std::vector<flux::Vec3f> points{flux::Vec3f{0.0F, 0.0F, 0.0F}};
    std::vector<flux::Vec3u> triangles;
    for (auto segment = 0U; segment < numSegments; ++segment) {
        auto const rim = pointOnUnitCircle(segment) * radius;
        points.emplace_back(rim.x(), rim.y(), 0.0F);
        triangles.emplace_back(0, 1 + nextSegment(segment), 1 + segment);
    }
    return createTriangleMesh(context, points, triangles);
}

[[nodiscard]] flux::Ref<flux::TriangleMesh> createSphere(flux::Context &context, float radius) {
    std::vector<flux::Vec3f> points{
        flux::Vec3f{0.0F, 0.0F, radius},
        flux::Vec3f{0.0F, 0.0F, -radius},
    };
    for (auto ring = 1U; ring < numRings; ++ring) {
        auto const polar =
            std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(numRings);
        for (auto segment = 0U; segment < numSegments; ++segment) {
            auto const rim = pointOnUnitCircle(segment) * (radius * std::sin(polar));
            points.emplace_back(rim.x(), rim.y(), radius * std::cos(polar));
        }
    }

    auto const first = [](std::uint32_t ring) { return 2 + (ring - 1) * numSegments; };
    std::vector<flux::Vec3u> triangles;
    for (auto segment = 0U; segment < numSegments; ++segment) {
        triangles.emplace_back(0, first(1) + segment, first(1) + nextSegment(segment));
        triangles.emplace_back(
            1, first(numRings - 1) + nextSegment(segment), first(numRings - 1) + segment
        );
    }
    for (auto ring = 1U; ring + 1 < numRings; ++ring) {
        for (auto segment = 0U; segment < numSegments; ++segment) {
            auto const upper = first(ring);
            auto const lower = first(ring + 1);
            triangles.emplace_back(upper + segment, lower + segment, upper + nextSegment(segment));
            triangles.emplace_back(
                upper + nextSegment(segment), lower + segment, lower + nextSegment(segment)
            );
        }
    }
    return createTriangleMesh(context, points, triangles);
}

[[nodiscard]] flux::Ref<flux::TriangleMesh>
createCylinder(flux::Context &context, float radius, float length) {
    std::vector<flux::Vec3f> points;
    for (auto const x : {-0.5F * length, 0.5F * length}) {
        for (auto segment = 0U; segment < numSegments; ++segment) {
            auto const rim = pointOnUnitCircle(segment) * radius;
            points.emplace_back(x, rim.x(), rim.y());
        }
    }

    std::vector<flux::Vec3u> triangles;
    for (auto segment = 0U; segment < numSegments; ++segment) {
        auto const back = segment;
        auto const backNext = nextSegment(segment);
        auto const front = numSegments + segment;
        auto const frontNext = numSegments + nextSegment(segment);
        triangles.emplace_back(back, backNext, front);
        triangles.emplace_back(backNext, frontNext, front);
    }
    return createTriangleMesh(context, points, triangles);
}

[[nodiscard]] float
worldArea(flux::TriangleMesh const &mesh, std::array<float, 12> const &transform) {
    auto const transformVector = [&](flux::Vec3f const &vector) {
        return flux::Vec3f{
            transform[0] * vector.x() + transform[1] * vector.y() + transform[2] * vector.z(),
            transform[4] * vector.x() + transform[5] * vector.y() + transform[6] * vector.z(),
            transform[8] * vector.x() + transform[9] * vector.y() + transform[10] * vector.z(),
        };
    };

    auto const vertices = mesh.getVertices();
    auto area = 0.0F;
    for (auto const &triangle : mesh.getTriangles()) {
        auto const &vertex0 = vertices[triangle[0u]];
        auto const edge1 = transformVector(vertices[triangle[1u]] - vertex0);
        auto const edge2 = transformVector(vertices[triangle[2u]] - vertex0);
        area += 0.5F * flux::cross(edge1, edge2).norm();
    }
    return area;
}

template <typename T>
[[nodiscard]] T getLightParam(
    HdSceneDelegate &sceneDelegate, SdfPath const &id, TfToken const &token, T const &fallback
) {
    return sceneDelegate.GetLightParamValue(id, token).GetWithDefault(fallback);
}

[[nodiscard]] flux::Spectrum
computeNormalizedEmission(HdSceneDelegate &sceneDelegate, SdfPath const &id, float area) {
    auto const radiance = HdFluxComputeLightEmission(sceneDelegate, id);
    if (getLightParam(sceneDelegate, id, HdLightTokens->normalize, false) && area > 0.0F)
        return radiance / area;
    return radiance;
}
} // namespace

HdFluxAreaLight::HdFluxAreaLight(SdfPath const &id, TfToken const &type) : HdLight(id) {
    if (type == HdPrimTypeTokens->diskLight)
        shapeSpec_ = DiskSpec{};
    else if (type == HdPrimTypeTokens->sphereLight)
        shapeSpec_ = SphereSpec{};
    else if (type == HdPrimTypeTokens->cylinderLight)
        shapeSpec_ = CylinderSpec{};
}

HdFluxAreaLight::~HdFluxAreaLight() = default;

void HdFluxAreaLight::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    try {
        auto const context = param.GetContext();

        auto const shapeSpec = getShapeSpec(*sceneDelegate);
        if (!primitive_)
            createPrimitive(*context, shapeSpec);
        else if (shapeSpec != shapeSpec_)
            primitive_->setGeometry(createShapeMesh(*context, shapeSpec));
        shapeSpec_ = shapeSpec;

        primitive_->setVisible(sceneDelegate->GetVisible(id));
        primitive_->setTransform(HdFluxToTransform(sceneDelegate->GetTransform(id)));

        auto const mesh = primitive_->getGeometry().dynamicCast<flux::TriangleMesh const>();
        auto const area = worldArea(*mesh, primitive_->getTransform());
        edf_->setRadiance(computeNormalizedEmission(*sceneDelegate, id, area));
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits HdFluxAreaLight::GetInitialDirtyBitsMask() const { return AllDirty; }

void HdFluxAreaLight::Finalize(HdRenderParam *renderParam) {
    if (!primitive_)
        return;

    auto const context = static_cast<HdFluxRenderParam *>(renderParam)->GetContext();
    context->remove(primitive_->getContextId());
}

HdFluxAreaLight::ShapeSpec HdFluxAreaLight::getShapeSpec(HdSceneDelegate &sceneDelegate) const {
    auto const get = [&](TfToken const &token, float fallback) {
        return std::abs(getLightParam(sceneDelegate, GetId(), token, fallback));
    };
    return std::visit(
        kira::Overload{
            [&](RectSpec) -> ShapeSpec {
        return RectSpec{
            .width = get(HdLightTokens->width, 1.0F), .height = get(HdLightTokens->height, 1.0F)
        };
    },
            [&](DiskSpec) -> ShapeSpec { return DiskSpec{get(HdLightTokens->radius, 0.5F)}; },
            [&](SphereSpec) -> ShapeSpec { return SphereSpec{get(HdLightTokens->radius, 0.5F)}; },
            [&](CylinderSpec) -> ShapeSpec {
        return CylinderSpec{
            .radius = get(HdLightTokens->radius, 0.5F), .length = get(HdLightTokens->length, 1.0F)
        };
    },
        },
        shapeSpec_
    );
}

flux::Ref<flux::TriangleMesh>
HdFluxAreaLight::createShapeMesh(flux::Context &context, ShapeSpec const &shapeSpec) {
    auto const create = kira::Overload{
        [&](RectSpec const &spec) { return createRectangle(context, spec.width, spec.height); },
        [&](DiskSpec const &spec) { return createDisk(context, spec.radius); },
        [&](SphereSpec const &spec) { return createSphere(context, spec.radius); },
        [&](CylinderSpec const &spec) { return createCylinder(context, spec.radius, spec.length); },
    };
    return std::visit(create, shapeSpec);
}

void HdFluxAreaLight::createPrimitive(flux::Context &context, ShapeSpec const &shapeSpec) {
    edf_ = context.create<flux::ConstantEDF>(kira::Properties{});

    auto primitive = context.create<flux::Primitive>(createShapeMesh(context, shapeSpec));
    primitive->setEDF(edf_);
    primitive_ = std::move(primitive);
}
