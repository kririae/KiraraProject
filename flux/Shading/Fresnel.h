#pragma once

/// \file flux/Shading/Fresnel.h
/// \brief Fresnel and direction utilities shared by scattering models.

#include <algorithm>
#include <cmath>
#include <tuple>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Evaluates exact unpolarized dielectric Fresnel reflectance.
///
/// \param cosThetaI Absolute cosine on the current side of the interface.
/// \param etaNext IOR on the next side divided by the IOR on the current side.
/// \pre \p cosThetaI is in \f$[0,1]\f$ and \p etaNext is positive.
/// \return Reflectance, transmitted cosine, and `1 / etaNext`.
[[nodiscard]] KIRA_HOST_DEVICE inline std::tuple<float, float, float>
fresnelDielectric(float cosThetaI, float etaNext) noexcept {
    auto const invEta = 1.0F / etaNext;
    if (etaNext == 1.0F)
        return {0.0F, cosThetaI, invEta};

    auto const sinThetaT2 = invEta * invEta * std::max(0.0F, 1.0F - cosThetaI * cosThetaI);
    if (sinThetaT2 >= 1.0F)
        return {1.0F, 0.0F, invEta};

    auto const cosThetaT = std::sqrt(1.0F - sinThetaT2);
    auto const rPar = (etaNext * cosThetaI - cosThetaT) / (etaNext * cosThetaI + cosThetaT);
    auto const rPerp = (cosThetaI - etaNext * cosThetaT) / (cosThetaI + etaNext * cosThetaT);
    return {0.5F * (rPar * rPar + rPerp * rPerp), cosThetaT, invEta};
}

/// \brief Returns Schlick's fifth-power angular weight.
///
/// \pre \p cosine is the absolute cosine between a direction and the interface normal.
[[nodiscard]] KIRA_HOST_DEVICE inline float schlickWeight(float cosTheta) noexcept {
    auto const x = std::clamp(1.0F - cosTheta, 0.0F, 1.0F);
    auto const x2 = x * x;
    return x2 * x2 * x;
}

/// \brief Approximates Fresnel reflectance from its value at normal incidence.
template <typename T>
[[nodiscard]] KIRA_HOST_DEVICE inline T schlick(T const &r0, float cosTheta) noexcept {
    return r0 + (T{1.0F} - r0) * schlickWeight(cosTheta);
}

/// \brief Reflects an away-pointing direction about \p normal.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
reflect(Vec3f const &direction, Vec3f const &normal) noexcept {
    return normal * (2.0F * direction.dot(normal)) - direction;
}

/// \brief Refracts an away-pointing direction through a dielectric interface.
///
/// \pre \p normal points into the current side and the interface permits transmission.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
refract(Vec3f const &direction, Vec3f const &normal, float cosThetaT, float invEta) noexcept {
    auto const cosThetaI = direction.dot(normal);
    return normal * (cosThetaI * invEta - cosThetaT) - direction * invEta;
}
} // namespace flux
