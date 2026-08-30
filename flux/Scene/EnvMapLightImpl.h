#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "flux/Core/MathUtils.h"
#include "flux/Scene/EnvMapLight.h"

namespace flux {
/*
 * Environment-map coordinates
 * ---------------------------
 *
 * World space is right-handed with +Y up. Environment-local space is also
 * right-handed, with +Z as the north pole. Its spherical coordinates are:
 *
 *     u = phi / (2 pi)
 *     v = theta / pi
 *
 * theta runs from the north pole to the south pole, so environment v is
 * top-down:
 *
 *     0-------------+u
 *     |  +Y ----------------------- +Y
 *     |  |
 *     |  +Z       -X -Z +X          +Z
 *     |  |
 *     +v -Y ----------------------- -Y
 *
 * In environment-local space, the same map is:
 *
 *     0-------------+u
 *     |  +Z ----------------------- +Z
 *     |  |
 *     |  +X       +Y -X -Y          +X
 *     |  |
 *     +v -Z ----------------------- -Z
 *
 * Image textures use bottom-up v after image orientation is applied. Only
 * texture lookup converts environment v with `1 - v`; sampling and PDFs keep
 * the spherical convention above.
 */
KIRA_HOST_DEVICE inline Vec3f EnvMapLight::Impl::toLocal(Vec3f const &w) const noexcept {
    return transformVec(worldToEnv.data(), w);
}

KIRA_HOST_DEVICE inline Vec3f EnvMapLight::Impl::toWorld(Vec3f const &w) const noexcept {
    return transformTransposeVec(worldToEnv.data(), w);
}

KIRA_HOST_DEVICE inline Vec2f EnvMapLight::Impl::directionToUV(Vec3f const &w) noexcept {
    auto phi = std::atan2(w.y(), w.x());
    if (phi < 0.0F)
        phi += 2.0F * std::numbers::pi_v<float>;
    auto const theta = std::acos(std::clamp(w.z(), -1.0F, 1.0F));
    return {
        phi / (2.0F * std::numbers::pi_v<float>),
        theta * std::numbers::inv_pi_v<float>,
    };
}

KIRA_HOST_DEVICE inline Vec3f EnvMapLight::Impl::uvToDirection(Vec2f uv) noexcept {
    auto const theta = std::numbers::pi_v<float> * uv.y();
    auto const phi = 2.0F * std::numbers::pi_v<float> * uv.x();
    auto const sinTheta = std::sin(theta);
    return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), std::cos(theta)};
}

template <typename Evaluator>
KIRA_HOST_DEVICE inline Spectrum EnvMapLight::Impl::eval(Vec3f const &w) const noexcept {
    auto const uv = directionToUV(toLocal(w));
    auto const value = texture.template eval3f<Evaluator>(SurfaceInteraction{
        .uv = {uv.x(), 1.0F - uv.y()},
    });
    return value * scale;
}

template <typename Evaluator>
KIRA_HOST_DEVICE inline DirectLightSample
EnvMapLight::Impl::sampleDirect(LightSamplingContext const &ctx, Vec2f u) const noexcept {
    (void)ctx;
    auto localWi = Vec3f{};
    auto pdfValue = 0.0F;
    if (distribution) {
        float uvPDF;
        auto const uv = distribution.sample(u, uvPDF);
        localWi = uvToDirection(uv);
        auto const sinTheta = std::sin(std::numbers::pi_v<float> * uv.y());
        if (sinTheta > 0.0F)
            pdfValue =
                uvPDF / (2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * sinTheta);
    } else {
        localWi = uniformSampleSphere(u);
        pdfValue = 0.25F * std::numbers::inv_pi_v<float>;
    }
    if (!(pdfValue > 0.0F))
        return {};

    auto const wi = toWorld(localWi);
    return {
        .radiance = eval<Evaluator>(wi),
        .wi = wi,
        .distance = std::numeric_limits<float>::max(),
        .pdf = pdfValue,
    };
}

KIRA_HOST_DEVICE inline float EnvMapLight::Impl::pdf(Vec3f const &w) const noexcept {
    if (!distribution)
        return 0.25F * std::numbers::inv_pi_v<float>;

    auto const uv = directionToUV(toLocal(w));
    auto const sinTheta = std::sin(std::numbers::pi_v<float> * uv.y());
    if (!(sinTheta > 0.0F))
        return 0.0F;
    return distribution.pdf(uv) /
           (2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * sinTheta);
}
} // namespace flux
