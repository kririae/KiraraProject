#pragma once

#include <pxr/imaging/hd/material.h>

#include "flux/Core/Object.h"

namespace flux {
class BSDF;
class ShaderBSDF;
} // namespace flux

class HdFluxMaterial final : public pxr::HdMaterial {
public:
    explicit HdFluxMaterial(pxr::SdfPath const &id);
    ~HdFluxMaterial() override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    [[nodiscard]] flux::Ref<flux::BSDF const> GetBSDF() const;

private:
    flux::Ref<flux::ShaderBSDF> bsdf_;
};
