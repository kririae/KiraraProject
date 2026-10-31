#include "flux/Hydra/Light.h"

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
#include "flux/Hydra/Radiance.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/Transform.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/EDF.h"
#include "kira/Properties.h"
#include "kira/Utils.h"

// The Tf diagnostic macros name Tf without its namespace.
PXR_NAMESPACE_USING_DIRECTIVE

namespace {
constexpr std::uint32_t numSegments{64};
constexpr std::uint32_t numRings{32};

[[nodiscard]] flux::Ref<flux::TriangleMesh> makeTriangleMesh(
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
makeRectangle(flux::Context &context, float width, float height) {
    auto const halfWidth = width * 0.5F;
    auto const halfHeight = height * 0.5F;

    std::array const points{
        flux::Vec3f{-halfWidth, -halfHeight, 0.0F},
        flux::Vec3f{-halfWidth, halfHeight, 0.0F},
        flux::Vec3f{halfWidth, halfHeight, 0.0F},
        flux::Vec3f{halfWidth, -halfHeight, 0.0F},
    };
    std::array const triangles{flux::Vec3u{0, 1, 2}, flux::Vec3u{0, 2, 3}};
    return makeTriangleMesh(context, points, triangles);
}

/// The geometric normal is -Z.
[[nodiscard]] flux::Ref<flux::TriangleMesh> makeDisk(flux::Context &context, float radius) {
    std::vector<flux::Vec3f> points{flux::Vec3f{0.0F, 0.0F, 0.0F}};
    std::vector<flux::Vec3u> triangles;
    for (auto segment = 0U; segment < numSegments; ++segment) {
        auto const rim = pointOnUnitCircle(segment) * radius;
        points.push_back(flux::Vec3f{rim.x(), rim.y(), 0.0F});
        triangles.push_back(flux::Vec3u{0, 1 + nextSegment(segment), 1 + segment});
    }
    return makeTriangleMesh(context, points, triangles);
}

[[nodiscard]] flux::Ref<flux::TriangleMesh> makeSphere(flux::Context &context, float radius) {
    std::vector<flux::Vec3f> points{
        flux::Vec3f{0.0F, 0.0F, radius},
        flux::Vec3f{0.0F, 0.0F, -radius},
    };
    for (auto ring = 1U; ring < numRings; ++ring) {
        auto const polar =
            std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(numRings);
        for (auto segment = 0U; segment < numSegments; ++segment) {
            auto const rim = pointOnUnitCircle(segment) * (radius * std::sin(polar));
            points.push_back(flux::Vec3f{rim.x(), rim.y(), radius * std::cos(polar)});
        }
    }

    auto const first = [](std::uint32_t ring) { return 2 + (ring - 1) * numSegments; };
    std::vector<flux::Vec3u> triangles;
    for (auto segment = 0U; segment < numSegments; ++segment) {
        triangles.push_back(flux::Vec3u{0, first(1) + segment, first(1) + nextSegment(segment)});
        triangles.push_back(
            flux::Vec3u{
                1, first(numRings - 1) + nextSegment(segment), first(numRings - 1) + segment
            }
        );
    }
    for (auto ring = 1U; ring + 1 < numRings; ++ring) {
        for (auto segment = 0U; segment < numSegments; ++segment) {
            auto const upper = first(ring);
            auto const lower = first(ring + 1);
            triangles.push_back(
                flux::Vec3u{upper + segment, lower + segment, upper + nextSegment(segment)}
            );
            triangles.push_back(
                flux::Vec3u{
                    upper + nextSegment(segment), lower + segment, lower + nextSegment(segment)
                }
            );
        }
    }
    return makeTriangleMesh(context, points, triangles);
}

[[nodiscard]] flux::Ref<flux::TriangleMesh>
makeCylinder(flux::Context &context, float radius, float length) {
    std::vector<flux::Vec3f> points;
    for (auto const x : {-0.5F * length, 0.5F * length}) {
        for (auto segment = 0U; segment < numSegments; ++segment) {
            auto const rim = pointOnUnitCircle(segment) * radius;
            points.push_back(flux::Vec3f{x, rim.x(), rim.y()});
        }
    }

    std::vector<flux::Vec3u> triangles;
    for (auto segment = 0U; segment < numSegments; ++segment) {
        auto const back = segment;
        auto const backNext = nextSegment(segment);
        auto const front = numSegments + segment;
        auto const frontNext = numSegments + nextSegment(segment);
        triangles.push_back(flux::Vec3u{back, backNext, front});
        triangles.push_back(flux::Vec3u{backNext, frontNext, front});
    }
    return makeTriangleMesh(context, points, triangles);
}

[[nodiscard]] float
worldArea(flux::TriangleMesh const &mesh, std::array<float, 12> const &transform) {
    auto const apply = [&](flux::Vec3f const &vector) {
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
        auto const edge1 = apply(vertices[triangle[1u]] - vertex0);
        auto const edge2 = apply(vertices[triangle[2u]] - vertex0);
        area += 0.5F * flux::cross(edge1, edge2).norm();
    }
    return area;
}

template <typename T>
[[nodiscard]] T readParam(
    HdSceneDelegate &sceneDelegate, SdfPath const &id, TfToken const &token, T const &fallback
) {
    return sceneDelegate.GetLightParamValue(id, token).GetWithDefault(fallback);
}

[[nodiscard]] flux::Spectrum
readRadiance(HdSceneDelegate &sceneDelegate, SdfPath const &id, float area) {
    auto const radiance = HdFluxReadRadiance(sceneDelegate, id);
    if (readParam(sceneDelegate, id, HdLightTokens->normalize, false) && area > 0.0F)
        return radiance / area;
    return radiance;
}
} // namespace

HdFluxLight::HdFluxLight(SdfPath const &id, TfToken const &type) : HdLight(id) {
    if (type == HdPrimTypeTokens->diskLight)
        shapeSpec_ = DiskSpec{};
    else if (type == HdPrimTypeTokens->sphereLight)
        shapeSpec_ = SphereSpec{};
    else if (type == HdPrimTypeTokens->cylinderLight)
        shapeSpec_ = CylinderSpec{};
}

HdFluxLight::~HdFluxLight() = default;

void HdFluxLight::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    try {
        auto const context = param.GetContext();

        auto const shapeSpec = readShapeSpec(*sceneDelegate);
        if (!primitive_)
            build(*context, shapeSpec);
        else if (shapeSpec != shapeSpec_)
            primitive_->setGeometry(makeMesh(*context, shapeSpec));
        shapeSpec_ = shapeSpec;

        primitive_->setVisible(sceneDelegate->GetVisible(id));
        primitive_->setTransform(HdFluxToTransform(sceneDelegate->GetTransform(id)));

        auto const mesh = primitive_->getGeometry().dynamicCast<flux::TriangleMesh const>();
        auto const area = worldArea(*mesh, primitive_->getTransform());
        edf_->setRadiance(readRadiance(*sceneDelegate, id, area));
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits &= ~AllDirty;
}

HdDirtyBits HdFluxLight::GetInitialDirtyBitsMask() const { return AllDirty; }

void HdFluxLight::Finalize(HdRenderParam *renderParam) {
    if (!primitive_)
        return;

    auto const context = static_cast<HdFluxRenderParam *>(renderParam)->GetContext();
    context->remove(primitive_->getContextId());
}

HdFluxLight::ShapeSpec HdFluxLight::readShapeSpec(HdSceneDelegate &sceneDelegate) const {
    auto const read = [&](TfToken const &token, float fallback) {
        return std::abs(readParam(sceneDelegate, GetId(), token, fallback));
    };
    return std::visit(
        kira::Overload{
            [&](RectSpec) -> ShapeSpec {
        return RectSpec{read(HdLightTokens->width, 1.0F), read(HdLightTokens->height, 1.0F)};
    },
            [&](DiskSpec) -> ShapeSpec { return DiskSpec{read(HdLightTokens->radius, 0.5F)}; },
            [&](SphereSpec) -> ShapeSpec { return SphereSpec{read(HdLightTokens->radius, 0.5F)}; },
            [&](CylinderSpec) -> ShapeSpec {
        return CylinderSpec{read(HdLightTokens->radius, 0.5F), read(HdLightTokens->length, 1.0F)};
    },
        },
        shapeSpec_
    );
}

flux::Ref<flux::TriangleMesh>
HdFluxLight::makeMesh(flux::Context &context, ShapeSpec const &shapeSpec) {
    auto const make = kira::Overload{
        [&](RectSpec const &spec) { return makeRectangle(context, spec.width, spec.height); },
        [&](DiskSpec const &spec) { return makeDisk(context, spec.radius); },
        [&](SphereSpec const &spec) { return makeSphere(context, spec.radius); },
        [&](CylinderSpec const &spec) { return makeCylinder(context, spec.radius, spec.length); },
    };
    return std::visit(make, shapeSpec);
}

void HdFluxLight::build(flux::Context &context, ShapeSpec const &shapeSpec) {
    edf_ = context.create<flux::ConstantEDF>(kira::Properties{});

    auto primitive = context.create<flux::Primitive>(makeMesh(context, shapeSpec));
    primitive->setEDF(edf_);
    primitive_ = std::move(primitive);
}
