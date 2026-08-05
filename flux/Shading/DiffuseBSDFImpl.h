#pragma once

#include <numbers>

#include "flux/Core/MathUtils.h"
#include "flux/Shading/BSDF.h"

namespace flux {
KIRA_HOST_DEVICE inline BSDFEvaluation DiffuseBSDF::Impl::evalAndPdf(
    [[maybe_unused]] SurfaceInteraction const &isect, Vec3f const &wo, Vec3f const &wi
) const noexcept {
    if (wo.z() <= 0.0F || wi.z() <= 0.0F)
        return {};

    constexpr auto inversePi = std::numbers::inv_pi_v<float>;
    return {
        .value = reflectance * (wi.z() * inversePi),
        .pdf = cosineHemispherePdf(wi),
    };
}

KIRA_HOST_DEVICE inline BSDFSample DiffuseBSDF::Impl::sample(
    [[maybe_unused]] SurfaceInteraction const &isect, Vec3f const &wo, [[maybe_unused]] float u1,
    Vec2f const &u2
) const noexcept {
    if (wo.z() <= 0.0F)
        return {};

    auto const wi = cosineSampleHemisphere(u2);
    return {
        .weight = reflectance,
        .wi = wi,
        .pdf = cosineHemispherePdf(wi),
        .eta = 1.0F,
        .lobe = BSDFLobe::DiffuseReflection,
    };
}
} // namespace flux
