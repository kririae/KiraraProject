#pragma once

#include <pxr/imaging/hd/material.h>

#include "flux/Core/Object.h"

namespace flux {
class BSDF;
class PrincipledBSDF;
} // namespace flux

/// \brief A Hydra material that renders as a Principled BSDF.
class HdFluxMaterial final : public pxr::HdMaterial {
public:
    explicit HdFluxMaterial(pxr::SdfPath const &id);
    ~HdFluxMaterial() override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// \brief Returns the BSDF that the material edits in place, or null before its first sync.
    [[nodiscard]] flux::Ref<flux::BSDF const> GetBSDF() const;

private:
    flux::Ref<flux::PrincipledBSDF> bsdf_;
};
