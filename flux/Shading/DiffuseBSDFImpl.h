#pragma once

#include <numbers>

#include "flux/Core/MathUtils.h"
#include "flux/Shading/BSDF.h"

namespace flux {
template <typename Evaluator>
KIRA_HOST_DEVICE inline BSDFResult DiffuseBSDF::Impl::execute(
    SurfaceInteraction const &isect, Vec3f const &wo, Vec3f const &wi, bool eval,
    [[maybe_unused]] float u1, Vec2f const &u2
) const noexcept {
    if (wo.z() <= 0.0F)
        return {};

    auto const value = R.template eval3f<Evaluator>(isect);
    auto result = BSDFResult{};
    if (eval && wi.z() > 0.0F) {
        constexpr auto inversePi = std::numbers::inv_pi_v<float>;
        result.evaluation = {
            .value = value * (wi.z() * inversePi),
            .pdf = cosineHemispherePdf(wi),
        };
    }

    auto const sampledWi = cosineSampleHemisphere(u2);
    result.sample = {
        .weight = value,
        .wi = sampledWi,
        .pdf = cosineHemispherePdf(sampledWi),
        .eta = 1.0F,
        .lobe = BSDFLobe::DiffuseReflection,
    };
    return result;
}
} // namespace flux
