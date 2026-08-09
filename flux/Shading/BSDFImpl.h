#pragma once

#include "flux/Shading/BSDF.h"
#include "flux/Shading/DiffuseBSDFImpl.h"
#include "flux/Shading/PrincipledBSDFImpl.h"

namespace flux {
template <typename Evaluator>
KIRA_HOST_DEVICE inline BSDFResult BSDF::Dispatcher::execute(
    BSDF::Impl const &bsdf, SurfaceInteraction const &isect, Vec3f const &wo, Vec3f const &wi,
    bool eval, float u1, Vec2f const &u2
) const noexcept {
    auto selectedType = bsdf.type;
    if (types == bsdfTypeBit(BSDFType::Diffuse))
        selectedType = BSDFType::Diffuse;
    else if (types == bsdfTypeBit(BSDFType::Principled))
        selectedType = BSDFType::Principled;

    if (selectedType == BSDFType::Diffuse)
        return bsdf.storage.diffuse.template execute<Evaluator>(isect, wo, wi, eval, u1, u2);
    else if (selectedType == BSDFType::Principled)
        return bsdf.storage.principled.template execute<Evaluator>(isect, wo, wi, eval, u1, u2);
    KIRA_UNREACHABLE();
}
} // namespace flux
