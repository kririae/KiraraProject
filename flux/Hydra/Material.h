#pragma once

#include <pxr/imaging/hd/material.h>

#include "flux/Core/Object.h"

namespace flux {
class BSDF;
class EDF;
class ShaderBSDF;
class ShaderEDF;
} // namespace flux

/// \brief A material of the scene, which gives a BSDF and an EDF.
///
/// Both keep their identity across syncs, and a sync replaces their shader.
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

    [[nodiscard]] flux::Ref<flux::EDF const> GetEDF() const;

private:
    /// Once it is set, \c edf_ is set too.
    flux::Ref<flux::ShaderBSDF> bsdf_;
    flux::Ref<flux::ShaderEDF> edf_;
};
