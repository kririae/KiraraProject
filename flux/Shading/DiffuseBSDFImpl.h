#pragma once

#include <numbers>

#include "flux/Core/MathUtils.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Frame.h"

namespace flux {
KIRA_HOST_DEVICE inline BSDFEvaluation DiffuseBSDF::Impl::evaluateAndPdf_(
    BSDFState const &state, BSDFQuery const &query, Vec3f const &wi
) const noexcept {
    Frame const frame(state.shadingNormal);
    auto const localWo = frame.toLocal(query.wo);
    auto const localWi = frame.toLocal(wi);
    if (Frame::cosTheta(localWo) <= 0.0F || Frame::cosTheta(localWi) <= 0.0F)
        return {};

    constexpr auto inversePi = std::numbers::inv_pi_v<float>;
    return {
        .value = reflectance * (Frame::cosTheta(localWi) * inversePi),
        .pdf = cosineHemispherePdf(localWi),
    };
}

KIRA_HOST_DEVICE inline BSDFSample DiffuseBSDF::Impl::sample_(
    BSDFState const &state, BSDFQuery const &query, [[maybe_unused]] float lobeSample,
    Vec2f const &directionSample
) const noexcept {
    Frame const frame(state.shadingNormal);
    if (Frame::cosTheta(frame.toLocal(query.wo)) <= 0.0F)
        return {};

    auto const localWi = cosineSampleHemisphere(directionSample);
    return {
        .weight = reflectance,
        .wi = frame.toWorld(localWi),
        .pdf = cosineHemispherePdf(localWi),
        .eta = 1.0F,
        .lobe = BSDFLobe::DiffuseReflection,
    };
}
} // namespace flux
