#pragma once

#include <algorithm>
#include <cmath>
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

template <typename Evaluator>
KIRA_HOST_DEVICE inline Spectrum
EnvMapLight::Impl::evalUV(Vec2f uv, float angle, float sinTheta) const noexcept {
    auto du = 0.0F;
    if (angle > 0.0F) {
        // Every longitude meets at a pole.
        du = 1.0F;
        if (sinTheta > 0.0F)
            du = std::min(angle / (2.0F * std::numbers::pi_v<float> * sinTheta), 1.0F);
    }
    auto const value = texture.template eval3f<Evaluator>(TextureEvalContext{
        .uv = {uv.x(), 1.0F - uv.y()},
        .duvdx = {du, 0.0F},
        .duvdy = {0.0F, -angle * std::numbers::inv_pi_v<float>},
    });
    return value * scale;
}

KIRA_HOST_DEVICE inline float EnvMapLight::Impl::pdfUV(Vec2f uv, float sinTheta) const noexcept {
    if (!distribution)
        return 0.25F * std::numbers::inv_pi_v<float>;

    if (!(sinTheta > 0.0F))
        return 0.0F;
    return distribution.pdf(uv) /
           (2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * sinTheta);
}

template <typename Evaluator>
KIRA_HOST_DEVICE inline Spectrum
EnvMapLight::Impl::eval(Vec3f const &w, float angle) const noexcept {
    auto const localW = toLocal(w);
    auto sinTheta = 0.0F;
    if (angle > 0.0F)
        sinTheta = std::sqrt(localW.x() * localW.x() + localW.y() * localW.y());
    return evalUV<Evaluator>(directionToUV(localW), angle, sinTheta);
}

template <typename Evaluator>
KIRA_HOST_DEVICE inline Spectrum
EnvMapLight::Impl::evalAndPdf(Vec3f const &w, float &pdfValue) const noexcept {
    return evalAndPdf<Evaluator>(w, 0.0F, pdfValue);
}

template <typename Evaluator>
KIRA_HOST_DEVICE inline Spectrum
EnvMapLight::Impl::evalAndPdf(Vec3f const &w, float angle, float &pdfValue) const noexcept {
    auto const localW = toLocal(w);
    auto const uv = directionToUV(localW);
    auto const sinTheta = std::sqrt(localW.x() * localW.x() + localW.y() * localW.y());
    pdfValue = pdfUV(uv, sinTheta);
    return evalUV<Evaluator>(uv, angle, sinTheta);
}

template <typename Evaluator>
KIRA_HOST_DEVICE inline DirectLightSample
EnvMapLight::Impl::sampleDirect(LightSamplingContext const &ctx, Vec2f u) const noexcept {
    (void)ctx;
    auto uv = Vec2f{};
    auto localWi = Vec3f{};
    auto pdfValue = 0.0F;
    if (distribution) {
        float uvPdf;
        uv = distribution.sample(u, uvPdf);
        auto const theta = std::numbers::pi_v<float> * uv.y();
        auto const phi = 2.0F * std::numbers::pi_v<float> * uv.x();
        float sinTheta;
        float cosTheta;
        float sinPhi;
        float cosPhi;
        sinCos(theta, sinTheta, cosTheta);
        sinCos(phi, sinPhi, cosPhi);
        localWi = Vec3f{sinTheta * cosPhi, sinTheta * sinPhi, cosTheta};
        if (sinTheta > 0.0F)
            pdfValue =
                uvPdf / (2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * sinTheta);
    } else {
        localWi = uniformSampleSphere(u);
        uv = Vec2f{
            u.x(),
            std::acos(std::clamp(localWi.z(), -1.0F, 1.0F)) * std::numbers::inv_pi_v<float>,
        };
        pdfValue = 0.25F * std::numbers::inv_pi_v<float>;
    }
    if (!(pdfValue > 0.0F))
        return {};

    auto const wi = toWorld(localWi);
    return {
        .radiance = evalUV<Evaluator>(uv, 0.0F, 0.0F),
        .wi = wi,
        .pdf = pdfValue,
        .type = LightType::EnvMap,
    };
}

KIRA_HOST_DEVICE inline float EnvMapLight::Impl::pdf(Vec3f const &w) const noexcept {
    auto const localW = toLocal(w);
    auto const sinTheta = std::sqrt(localW.x() * localW.x() + localW.y() * localW.y());
    return pdfUV(directionToUV(localW), sinTheta);
}
} // namespace flux
