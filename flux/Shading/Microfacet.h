#pragma once

/// \file flux/Shading/Microfacet.h
/// \brief Isotropic GGX evaluation and visible-normal sampling.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "flux/Core/MathUtils.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Isotropic GGX distribution with visible-normal sampling.
class GGXDistribution {
public:
    /// \param alpha Microfacet slope roughness.
    /// \pre \p alpha is positive.
    KIRA_HOST_DEVICE explicit GGXDistribution(float alpha) noexcept : alpha_(alpha) {}

    /// \brief Returns the GGX normal distribution for \p m.
    [[nodiscard]] KIRA_HOST_DEVICE float eval(Vec3f const &m) const noexcept {
        if (m.z() <= 0.0F)
            return 0.0F;

        auto const alpha2 = alpha_ * alpha_;
        auto const denom = 1.0F + (alpha2 - 1.0F) * m.z() * m.z();
        return alpha2 / (std::numbers::pi_v<float> * denom * denom);
    }

    /// \brief Returns the Smith masking term for one direction.
    [[nodiscard]] KIRA_HOST_DEVICE float smithG1(Vec3f const &v, Vec3f const &m) const noexcept {
        if (v.dot(m) * v.z() <= 0.0F)
            return 0.0F;

        auto const xy2 = v.x() * v.x() + v.y() * v.y();
        if (xy2 == 0.0F)
            return 1.0F;

        auto const tanTheta2 = xy2 / (v.z() * v.z());
        return 2.0F / (1.0F + std::sqrt(1.0F + alpha_ * alpha_ * tanTheta2));
    }

    /// \brief Returns the separable Smith geometry term.
    [[nodiscard]] KIRA_HOST_DEVICE float
    G(Vec3f const &wo, Vec3f const &wi, Vec3f const &m) const noexcept {
        return smithG1(wo, m) * smithG1(wi, m);
    }

    /// \brief Returns the visible-normal PDF with respect to solid angle at \p m.
    ///
    /// \pre \p wo lies above the local surface.
    [[nodiscard]] KIRA_HOST_DEVICE float pdf(Vec3f const &wo, Vec3f const &m) const noexcept {
        if (wo.z() <= 0.0F)
            return 0.0F;
        return eval(m) * smithG1(wo, m) * std::abs(wo.dot(m)) / wo.z();
    }

    /// \brief Samples the GGX distribution of visible normals.
    ///
    /// Implements the projected-area construction from Heitz 2018. Both the
    /// input direction and returned normal lie above the local surface.
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f sample(Vec3f const &wo, Vec2f const &u) const noexcept {
        // Stretch the view so the distribution becomes isotropic.
        auto const view = Vec3f{alpha_ * wo.x(), alpha_ * wo.y(), wo.z()}.normalize();
        auto disk = uniformSampleDisk(u);
        auto const blend = 0.5F * (1.0F + view.z());
        disk.y() = (1.0F - blend) * std::sqrt(std::max(0.0F, 1.0F - disk.x() * disk.x())) +
                   blend * disk.y();

        // Project the warped disk sample to a visible slope.
        auto const projectedZ = std::sqrt(std::max(0.0F, 1.0F - disk.norm2()));
        auto const sinTheta = std::sqrt(std::max(0.0F, 1.0F - view.z() * view.z()));
        auto const inverseProjection = 1.0F / (sinTheta * disk.y() + view.z() * projectedZ);
        auto const slope = Vec2f{
            (view.z() * disk.y() - sinTheta * projectedZ) * inverseProjection,
            disk.x() * inverseProjection,
        };

        auto cosPhi = 1.0F;
        auto sinPhi = 0.0F;
        auto const xyLength = std::sqrt(view.x() * view.x() + view.y() * view.y());
        if (xyLength > 0.0F) {
            cosPhi = view.x() / xyLength;
            sinPhi = view.y() / xyLength;
        }

        auto const rotatedSlope = Vec2f{
            (cosPhi * slope.x() - sinPhi * slope.y()) * alpha_,
            (sinPhi * slope.x() + cosPhi * slope.y()) * alpha_,
        };
        return Vec3f{-rotatedSlope.x(), -rotatedSlope.y(), 1.0F}.normalize();
    }

private:
    float alpha_;
};

/// \brief Half-vector geometry shared by microfacet value and PDF calculations.
struct MicrofacetGeometry {
    /// Microfacet normal.
    Vec3f m;
    float woDotM{};
    float wiDotM{};

    /// Solid-angle Jacobian from \c m to \c wi.
    float dMdWi{};

    bool reflection{};
    bool valid{};
};

/// \brief Builds reflection or transmission half-vector geometry.
///
/// \param etaNext IOR on the next side divided by the IOR on the current side.
/// \pre \p wo and \p wi are normalized shading-local directions.
[[nodiscard]] KIRA_HOST_DEVICE inline MicrofacetGeometry
makeMicrofacetGeometry(Vec3f const &wo, Vec3f const &wi, float etaNext) noexcept {
    auto const reflection = wo.z() * wi.z() > 0.0F;
    auto m = wo + wi * (reflection ? 1.0F : etaNext);
    if (m.norm2() == 0.0F)
        return {};
    m = m.normalize();
    if (m.z() < 0.0F)
        m = -m;

    auto const woDotM = wo.dot(m);
    auto const wiDotM = wi.dot(m);
    if (woDotM * wo.z() <= 0.0F || wiDotM * wi.z() <= 0.0F)
        return {};

    auto dMdWi = 0.0F;
    if (reflection) {
        if (wiDotM != 0.0F)
            dMdWi = 1.0F / (4.0F * std::abs(wiDotM));
    } else {
        auto const denom = woDotM + etaNext * wiDotM;
        if (denom != 0.0F)
            dMdWi = etaNext * etaNext * std::abs(wiDotM) / (denom * denom);
    }

    return {
        .m = m,
        .woDotM = woDotM,
        .wiDotM = wiDotM,
        .dMdWi = dMdWi,
        .reflection = reflection,
        .valid = dMdWi > 0.0F,
    };
}
} // namespace flux
