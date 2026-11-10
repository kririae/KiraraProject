#pragma once

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/imaging/hd/light.h>

#include <variant>
#include <vector>

#include "flux/Core/Object.h"

namespace flux {
class ConstantEDF;
class Context;
class Primitive;
class TriangleMesh;
} // namespace flux

/// \brief A Hydra rect, disk, sphere, or cylinder light that renders as an emissive mesh, once per
/// instance.
///
/// \c edfs_ holds the EDF of each primitive of \c primitives_. After a sync that throws nothing,
/// \c primitives_ holds one primitive per element of \c instanceTransforms_. Primitive \c i has the
/// geometry \c geometry_, the transform of the light followed by \c instanceTransforms_[i], and an
/// EDF whose radiance \c normalize divides by the world area of that primitive.
class HdFluxAreaLight final : public pxr::HdLight {
public:
    /// \pre \p type is \c rectLight, \c diskLight, \c sphereLight, or \c cylinderLight.
    HdFluxAreaLight(pxr::SdfPath const &id, pxr::TfToken const &type);
    ~HdFluxAreaLight() override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(pxr::HdRenderParam *renderParam) override;

private:
    struct RectSpec {
        float width{};
        float height{};
        friend bool operator==(RectSpec const &, RectSpec const &) = default;
    };

    struct DiskSpec {
        float radius{};
        friend bool operator==(DiskSpec const &, DiskSpec const &) = default;
    };

    struct SphereSpec {
        float radius{};
        friend bool operator==(SphereSpec const &, SphereSpec const &) = default;
    };

    struct CylinderSpec {
        float radius{};
        float length{};
        friend bool operator==(CylinderSpec const &, CylinderSpec const &) = default;
    };

    using ShapeSpec = std::variant<RectSpec, DiskSpec, SphereSpec, CylinderSpec>;

    [[nodiscard]] ShapeSpec getShapeSpec(pxr::HdSceneDelegate &sceneDelegate) const;
    [[nodiscard]] static flux::Ref<flux::TriangleMesh>
    createShapeMesh(flux::Context &context, ShapeSpec const &shapeSpec);

    /// Null until a sync builds the shape.
    flux::Ref<flux::TriangleMesh const> geometry_;

    /// The shape of \c geometry_. Its type is the type of the light.
    ShapeSpec shapeSpec_;

    pxr::VtMatrix4dArray instanceTransforms_;

    std::vector<flux::Ref<flux::Primitive>> primitives_;
    std::vector<flux::Ref<flux::ConstantEDF>> edfs_;
};
