#pragma once

#include <pxr/imaging/hd/material.h>

#include "flux/Core/Object.h"

namespace flux {
class BSDF;
} // namespace flux

/// \brief A Hydra material. It gives no BSDF yet, so the meshes bound to it render the fallback.
class HdFluxMaterial final : public pxr::HdMaterial {
public:
    explicit HdFluxMaterial(pxr::SdfPath const &id);
    ~HdFluxMaterial() override;

    void Sync(
        pxr::HdSceneDelegate *sceneDelegate, pxr::HdRenderParam *renderParam,
        pxr::HdDirtyBits *dirtyBits
    ) override;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// \brief Returns the BSDF that the material edits in place, or null.
    [[nodiscard]] flux::Ref<flux::BSDF const> GetBSDF() const;

private:
    flux::Ref<flux::BSDF> bsdf_;
};
