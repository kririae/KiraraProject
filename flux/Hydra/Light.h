#pragma once

#include <pxr/imaging/hd/light.h>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"

namespace flux {
class ConstantEDF;
class Context;
class Primitive;
} // namespace flux

/// \brief A Hydra rect light that renders as an emissive rectangle.
///
/// The rectangle lies in the XY plane of the light and emits toward -Z.
class HdFluxLight final : public pxr::HdLight {
public:
    explicit HdFluxLight(pxr::SdfPath const &id);
    ~HdFluxLight() override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Finalize(pxr::HdRenderParam *renderParam) override;

private:
    void build(flux::Context &context, flux::Vec2f const &size);

    /// Null until a sync builds the light.
    flux::Ref<flux::Primitive> primitive_;
    flux::Ref<flux::ConstantEDF> edf_;

    /// The width and the height of the rectangle in the geometry of \c primitive_.
    flux::Vec2f size_{};
};
