#pragma once

/// \file flux/Shading/PrincipledBSDFImpl.h
/// \brief Disney/Burley Principled BSDF implementation.
///
/// Directions point away from the surface and use a shading-local frame whose
/// normal is the positive z axis. The implementation follows Burley 2012/2015,
/// Walter et al. 2007 for rough transmission, and Heitz 2018 for GGX visible
/// normal sampling.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "flux/Core/MathUtils.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Fresnel.h"
#include "flux/Shading/Microfacet.h"

namespace flux::principled {
struct Parameters {
    Spectrum baseColor;       // (1)
    float roughness;          // (2)
    float metallic;           // (3)
    float specTrans;          // (4)
    float specTint;           // (5)
    float sheen;              // (6)
    float sheenTint;          // (7)
    float flatness;           // (8)
    float clearcoat;          // (9)
    float clearcoatRoughness; // (10)
    float eta;                // (11)
};

struct LobeWeights {
    float diffuse{};
    float clearcoat{};
    float specular{};
};

/// The main microfacet term is one sampling family. Reflection and
/// transmission are selected from the sampled normal using exact Fresnel.
[[nodiscard]] KIRA_HOST_DEVICE inline LobeWeights
lobeWeights(bool frontSide, float brdfWeight, float bsdfWeight, float clearcoat) noexcept {
    if (!frontSide) {
        if (bsdfWeight > 0.0F)
            return {.specular = 1.0F};
        return {};
    }

    auto result = LobeWeights{
        .diffuse = brdfWeight,
        .clearcoat = 0.25F * clearcoat,
        .specular = 1.0F,
    };
    auto const total = result.diffuse + result.clearcoat + result.specular;
    result.diffuse /= total;
    result.clearcoat /= total;
    result.specular /= total;
    return result;
}

struct SpecularWeights {
    float reflection{};
    float transmission{};
};

/// Splits the main GGX family after sampling its normal. The two weights sum
/// to one whenever that family is active.
[[nodiscard]] KIRA_HOST_DEVICE inline SpecularWeights
specularWeights(bool frontSide, float bsdfWeight, float F) noexcept {
    if (bsdfWeight == 0.0F)
        return frontSide ? SpecularWeights{.reflection = 1.0F} : SpecularWeights{};
    if (!frontSide)
        return {.reflection = F, .transmission = 1.0F - F};

    auto const transmission = bsdfWeight * (1.0F - F);
    return {.reflection = 1.0F - transmission, .transmission = transmission};
}

[[nodiscard]] KIRA_HOST_DEVICE inline float gtr1(Vec3f const &m, float alpha) noexcept {
    if (m.z() <= 0.0F)
        return 0.0F;

    auto const alpha2 = alpha * alpha;
    auto const denom =
        std::numbers::pi_v<float> * std::log(alpha2) * (1.0F + (alpha2 - 1.0F) * m.z() * m.z());
    auto const value = (alpha2 - 1.0F) / denom;
    return value * m.z() > 1.0e-20F ? value : 0.0F;
}

[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f sampleGTR1(Vec2f const &u, float alpha) noexcept {
    auto const alpha2 = alpha * alpha;
    auto const cosTheta2 = (1.0F - std::pow(alpha2, 1.0F - u.y())) / (1.0F - alpha2);
    auto const sinTheta = std::sqrt(std::max(0.0F, 1.0F - cosTheta2));
    auto const cosTheta = std::sqrt(std::max(0.0F, cosTheta2));
    auto const phi = 2.0F * std::numbers::pi_v<float> * u.x();
    return {std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
}

[[nodiscard]] KIRA_HOST_DEVICE inline float clearcoatG1(Vec3f const &v, Vec3f const &m) noexcept {
    if (v.dot(m) * v.z() <= 0.0F)
        return 0.0F;

    auto const cosTheta = std::abs(v.z());
    if (cosTheta == 1.0F)
        return 1.0F;

    constexpr auto alpha = 0.25F;
    auto const tanTheta2 = (1.0F - cosTheta * cosTheta) / (cosTheta * cosTheta);
    return 2.0F / (1.0F + std::sqrt(1.0F + alpha * alpha * tanTheta2));
}

[[nodiscard]] KIRA_HOST_DEVICE inline Spectrum principledFresnel(
    Parameters const &params, bool frontSide, float F, float cosTheta, float bsdfWeight
) noexcept {
    if (!frontSide)
        return Spectrum{bsdfWeight * F};

    auto result = Spectrum{};
    if (params.metallic > 0.0F)
        result = result + schlick(params.baseColor, cosTheta) * params.metallic;

    if (params.specTint > 0.0F) {
        auto const lum = luminance(params.baseColor);
        auto const tint = lum > 0.0F ? params.baseColor / lum : Spectrum{1.0F};
        auto const r0 = (params.eta - 1.0F) / (params.eta + 1.0F);
        result = result +
                 schlick(tint * (r0 * r0), cosTheta) * ((1.0F - params.metallic) * params.specTint);
    }

    return result + Spectrum{(1.0F - params.metallic) * (1.0F - params.specTint) * F};
}

[[nodiscard]] KIRA_HOST_DEVICE inline BSDFEvaluation evalAndPdf(
    Parameters const &params, Vec3f const &wo, Vec3f const &wi, float etaNext, float brdfWeight,
    float bsdfWeight, LobeWeights const &lobes, GGXDistribution const &ggx
) noexcept {
    auto const cosWo = wo.z();
    auto const cosWi = wi.z();
    if (cosWo == 0.0F || cosWi == 0.0F)
        return {};

    auto const geometry = makeMicrofacetGeometry(wo, wi, etaNext);
    if (!geometry.valid)
        return {};

    auto const frontSide = cosWo > 0.0F;
    auto const orientedWo = frontSide ? wo : -wo;
    auto const F = std::get<0>(fresnelDielectric(std::abs(geometry.woDotM), etaNext));
    auto const specular = specularWeights(frontSide, bsdfWeight, F);
    auto const microfacetPdf = ggx.pdf(orientedWo, geometry.m) * geometry.dMdWi;
    auto const D = ggx.eval(geometry.m);
    auto const G = ggx.G(wo, wi, geometry.m);

    auto result = BSDFEvaluation{};

    if (geometry.reflection) {
        auto const Fr =
            principledFresnel(params, frontSide, F, std::abs(geometry.woDotM), bsdfWeight);
        result.value = result.value + Fr * (D * G / (4.0F * std::abs(cosWo)));
        result.pdf += lobes.specular * specular.reflection * microfacetPdf;
    } else if (specular.transmission > 0.0F) {
        auto const denom = geometry.wiDotM + geometry.woDotM / etaNext;
        auto const factor = (1.0F - F) * D * G *
                            std::abs(geometry.wiDotM * geometry.woDotM / (cosWo * denom * denom)) /
                            (etaNext * etaNext);
        result.value = result.value + params.baseColor.sqrt() * (bsdfWeight * factor);
        result.pdf += lobes.specular * specular.transmission * microfacetPdf;
    }

    if (!geometry.reflection || !frontSide)
        return result;

    // GTR1 clearcoat uses a fixed GGX masking roughness.
    if (params.clearcoat > 0.0F) {
        auto const alpha = 0.001F + 0.099F * params.clearcoatRoughness;
        auto const Dc = gtr1(geometry.m, alpha);
        auto const Fc = schlick(0.04F, std::abs(geometry.woDotM));
        auto const Gc = clearcoatG1(wo, geometry.m) * clearcoatG1(wi, geometry.m);
        result.value =
            result.value + Spectrum{0.25F * params.clearcoat * Fc * Dc * Gc * std::abs(cosWi)};
        result.pdf += lobes.clearcoat * Dc * geometry.m.z() * geometry.dMdWi;
    }

    if (brdfWeight <= 0.0F)
        return result;

    // Burley diffuse includes retro-reflection and the optional fake-subsurface term.
    auto const woWeight = schlickWeight(std::abs(cosWo));
    auto const wiWeight = schlickWeight(std::abs(cosWi));
    auto const wiDotM = geometry.m.dot(wi);
    auto const retro = 2.0F * params.roughness * wiDotM * wiDotM;
    auto const diffuse = (1.0F - 0.5F * woWeight) * (1.0F - 0.5F * wiWeight);
    auto const retroReflection =
        retro * (woWeight + wiWeight + woWeight * wiWeight * (retro - 1.0F));
    auto diffuseShape = diffuse + retroReflection;

    if (params.flatness > 0.0F) {
        auto const fss90 = 0.5F * retro;
        auto const fssWo = 1.0F + (fss90 - 1.0F) * woWeight;
        auto const fssWi = 1.0F + (fss90 - 1.0F) * wiWeight;
        auto const subsurface =
            1.25F * (fssWo * fssWi * (1.0F / (std::abs(cosWo) + std::abs(cosWi)) - 0.5F) + 0.5F);
        diffuseShape += (subsurface - diffuseShape) * params.flatness;
    }

    result.value = result.value + params.baseColor * (brdfWeight * std::abs(cosWi) *
                                                      std::numbers::inv_pi_v<float> * diffuseShape);
    result.pdf += lobes.diffuse * cosineHemispherePdf(wi);

    if (params.sheen > 0.0F) {
        auto sheenColor = Spectrum{1.0F};
        if (params.sheenTint > 0.0F) {
            auto const lum = luminance(params.baseColor);
            auto const tint = lum > 0.0F ? params.baseColor / lum : Spectrum{1.0F};
            sheenColor = Spectrum{1.0F} + (tint - Spectrum{1.0F}) * params.sheenTint;
        }
        result.value =
            result.value + sheenColor * (params.sheen * (1.0F - params.metallic) *
                                         schlickWeight(std::abs(wiDotM)) * std::abs(cosWi));
    }

    return result;
}

[[nodiscard]] KIRA_HOST_DEVICE inline BSDFSample sample(
    Parameters const &params, Vec3f const &wo, float etaNext, float brdfWeight, float bsdfWeight,
    LobeWeights const &lobes, GGXDistribution const &ggx, float u1, Vec2f const &u2
) noexcept {
    auto wi = Vec3f{};
    auto lobe = BSDFLobe::None;
    auto sampleEta = 1.0F;

    if (u1 < lobes.diffuse) {
        wi = cosineSampleHemisphere(u2);
        lobe = BSDFLobe::DiffuseReflection;
    } else if (u1 < lobes.diffuse + lobes.clearcoat) {
        auto const alpha = 0.001F + 0.099F * params.clearcoatRoughness;
        wi = reflect(wo, sampleGTR1(u2, alpha));
        lobe = BSDFLobe::GlossyReflection;
    } else {
        // Reflection and transmission share one sampled GGX normal.
        auto const frontSide = wo.z() > 0.0F;
        auto const orientedWo = frontSide ? wo : -wo;
        auto const sampledM = ggx.sample(orientedWo, u2);
        auto const m = frontSide ? sampledM : -sampledM;
        auto const [F, cosThetaT, invEta] = fresnelDielectric(std::abs(wo.dot(m)), etaNext);
        auto const weights = specularWeights(frontSide, bsdfWeight, F);
        auto const uSpecular = (u1 - lobes.diffuse - lobes.clearcoat) / lobes.specular;

        if (uSpecular < weights.transmission) {
            if (etaNext == 1.0F) {
                auto const probability = lobes.specular * weights.transmission;
                return {
                    .weight = params.baseColor.sqrt() * (bsdfWeight / probability),
                    .wi = -wo,
                    .pdf = probability,
                    .eta = 1.0F,
                    .lobe = BSDFLobe::DeltaTransmission,
                };
            }
            wi = refract(wo, m, cosThetaT, invEta);
            sampleEta = etaNext;
            lobe = BSDFLobe::GlossyTransmission;
        } else {
            wi = reflect(wo, m);
            lobe = BSDFLobe::GlossyReflection;
        }
    }

    auto const evaluation = evalAndPdf(params, wo, wi, etaNext, brdfWeight, bsdfWeight, lobes, ggx);
    if (evaluation.pdf <= 0.0F)
        return {};

    return {
        .weight = evaluation.value / evaluation.pdf,
        .wi = wi,
        .pdf = evaluation.pdf,
        .eta = sampleEta,
        .lobe = lobe,
    };
}
} // namespace flux::principled

namespace flux {
template <typename Evaluator>
KIRA_HOST_DEVICE inline BSDFResult PrincipledBSDF::Impl::execute(
    SurfaceInteraction const &isect, Vec3f const &wo, Vec3f const &wi, bool eval, float u1,
    Vec2f const &u2
) const noexcept {
    auto const params = principled::Parameters{
        .baseColor = baseColor.template eval3f<Evaluator>(isect),                   // (1)
        .roughness = roughness.template eval1f<Evaluator>(isect),                   // (2)
        .metallic = metallic.template eval1f<Evaluator>(isect),                     // (3)
        .specTrans = specTrans.template eval1f<Evaluator>(isect),                   // (4)
        .specTint = specTint.template eval1f<Evaluator>(isect),                     // (5)
        .sheen = sheen.template eval1f<Evaluator>(isect),                           // (6)
        .sheenTint = sheenTint.template eval1f<Evaluator>(isect),                   // (7)
        .flatness = flatness.template eval1f<Evaluator>(isect),                     // (8)
        .clearcoat = clearcoat.template eval1f<Evaluator>(isect),                   // (9)
        .clearcoatRoughness = clearcoatRoughness.template eval1f<Evaluator>(isect), // (10)
        .eta = eta,                                                                 // (11)
    };
    auto const frontSide = wo.z() > 0.0F;
    if (wo.z() == 0.0F)
        return {};

    auto const etaNext = frontSide ? params.eta : 1.0F / params.eta;
    auto const brdfWeight = (1.0F - params.metallic) * (1.0F - params.specTrans);
    auto const bsdfWeight = (1.0F - params.metallic) * params.specTrans;
    if (!frontSide && bsdfWeight == 0.0F)
        return {};

    auto const lobes = principled::lobeWeights(frontSide, brdfWeight, bsdfWeight, params.clearcoat);
    auto const ggx = GGXDistribution{params.roughness * params.roughness};

    auto result = BSDFResult{};
    if (eval)
        result.evaluation =
            principled::evalAndPdf(params, wo, wi, etaNext, brdfWeight, bsdfWeight, lobes, ggx);

    result.sample =
        principled::sample(params, wo, etaNext, brdfWeight, bsdfWeight, lobes, ggx, u1, u2);
    return result;
}
} // namespace flux
