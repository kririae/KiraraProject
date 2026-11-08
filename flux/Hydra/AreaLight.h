#pragma once

#include <pxr/imaging/hd/light.h>

#include <variant>

#include "flux/Core/Object.h"

namespace flux {
class ConstantEDF;
class Context;
class Primitive;
class TriangleMesh;
} // namespace flux

/// \brief A Hydra rect, disk, sphere, or cylinder light that renders as an emissive mesh.
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

    void createPrimitive(flux::Context &context, ShapeSpec const &shapeSpec);

    /// Null until a sync builds the light.
    flux::Ref<flux::Primitive> primitive_;
    flux::Ref<flux::ConstantEDF> edf_;

    /// The shape in the geometry of \c primitive_. Its type is the type of the light.
    ShapeSpec shapeSpec_;
};
