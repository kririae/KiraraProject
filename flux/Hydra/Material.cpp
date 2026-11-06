#include "flux/Hydra/Material.h"

#include "flux/Shading/BSDF.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdFluxMaterial::HdFluxMaterial(SdfPath const &id) : HdMaterial(id) {}

HdFluxMaterial::~HdFluxMaterial() = default;

void HdFluxMaterial::Sync(
    HdSceneDelegate * /*sceneDelegate*/, HdRenderParam * /*renderParam*/, HdDirtyBits *dirtyBits
) {
    *dirtyBits = Clean;
}

HdDirtyBits HdFluxMaterial::GetInitialDirtyBitsMask() const { return DirtyParams | DirtyResource; }

flux::Ref<flux::BSDF const> HdFluxMaterial::GetBSDF() const { return bsdf_; }
