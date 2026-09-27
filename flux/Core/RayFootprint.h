#pragma once

/// \file
/// \brief Ray-cone texture footprints.
///
/// The update rules follow the NVIDIA OptiX Toolkit ray-cone model, except that surface
/// curvature does not widen the cone. A per-triangle curvature estimate is unreliable on
/// meshes that generate smooth normals, and the term only affects near-delta lobes that
/// reflect or transmit onto a textured surface.
/// \see https://github.com/NVIDIA/optix-toolkit/tree/master/ShaderUtil/docs/rayCones

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "flux/Core/MathUtils.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Circular footprint carried by one ray.
///
/// Angle is the signed spread in radians under the small-angle approximation.
/// Width is the spatial extent at the ray origin. A negative angle converges.
struct RayCone {
    float angle{};
    float width{};

public:
    /// \brief Advances the cone by \p distance.
    ///
    /// The width follows \f$w' = w + \alpha d\f$. Both signs flip after the
    /// cone crosses its focus and starts to diverge.
    KIRA_HOST_DEVICE void propagate(float distance) noexcept {
        width += angle * distance;
        if (angle < 0.0F && width < 0.0F) {
            angle = -angle;
            width = -width;
        }
    }

    /// \brief Sets the diffuse-scattering limit.
    KIRA_HOST_DEVICE void setDiffuse() noexcept {
        angle = maxAngle;
        width = std::abs(width);
    }

    /// \brief Updates the angle after refraction through a surface.
    ///
    /// \p eta is the next IOR divided by the current IOR.
    KIRA_HOST_DEVICE void refract(float eta) noexcept {
        if (angle < maxAngle)
            angle /= eta;
    }

    /// \brief Broadens the cone after BSDF scattering.
    ///
    /// The spread grows as \p bsdf falls. Values no greater than
    /// \f$1/\pi\f$ use the diffuse limit.
    KIRA_HOST_DEVICE void scatter(float bsdf) noexcept {
        if (angle >= maxAngle)
            return;

        constexpr auto inversePi = std::numbers::inv_pi_v<float>;
        if (bsdf <= inversePi) {
            setDiffuse();
            return;
        }

        angle += maxAngle * inversePi / bsdf;
        if (angle >= maxAngle)
            setDiffuse();
    }

private:
    /// 0.25-radian diffuse spread from the NVIDIA OptiX Toolkit heuristic.
    static constexpr float maxAngle = 0.25F;
};

/// \brief Ray footprint carried as pixel and lens cones.
///
/// Surface projection uses the larger width so the footprint remains useful
/// when the lens cone reaches its focus.
struct RayFootprint {
    /// Both texture backends use CUDA's maximum anisotropy of 16.
    static constexpr int maxAnisotropy = 16;

    /// Pixel cone followed by lens cone.
    std::array<RayCone, 2> cones{};

    KIRA_HOST_DEVICE void propagate(float distance) noexcept {
        for (auto &cone : cones)
            cone.propagate(distance);
    }

    KIRA_HOST_DEVICE void setDiffuse() noexcept {
        for (auto &cone : cones)
            cone.setDiffuse();
    }

    KIRA_HOST_DEVICE void refract(float eta) noexcept {
        for (auto &cone : cones)
            cone.refract(eta);
    }

    KIRA_HOST_DEVICE void scatter(float bsdf) noexcept {
        for (auto &cone : cones)
            cone.scatter(bsdf);
    }

    /// \brief Returns the larger absolute cone angle for environment-map filtering.
    [[nodiscard]] KIRA_HOST_DEVICE float angle() const noexcept {
        return std::max(std::abs(cones[0].angle), std::abs(cones[1].angle));
    }

    /// \brief Projects the larger cone width into two world-space surface offsets.
    ///
    /// \pre \p direction and \p normal are normalized.
    KIRA_HOST_DEVICE void
    project(Vec3f const &direction, Vec3f const &normal, Vec3f &dpdx, Vec3f &dpdy) const noexcept {
        constexpr auto invMaxAnisotropy = 1.0F / static_cast<float>(maxAnisotropy);
        auto const width = std::max(std::abs(cones[0].width), std::abs(cones[1].width));
        auto const cosine = direction.dot(normal);
        auto tangent = Vec3f{};
        if (std::abs(cosine) < 0.999F) {
            tangent = (direction - normal * cosine).normalize();
            dpdx = tangent * (width / std::max(std::abs(cosine), invMaxAnisotropy));
        } else {
            // The ray direction does not define a stable tangent near normal incidence.
            tangent = std::abs(normal.x()) > std::abs(normal.z())
                          ? Vec3f{-normal.y(), normal.x(), 0.0F}
                          : Vec3f{0.0F, -normal.z(), normal.y()};
            tangent = tangent.normalize();
            dpdx = tangent * width;
        }
        // Both vectors are unit and perpendicular, so their cross product is unit.
        dpdy = cross(tangent, normal) * width;
    }
};

/// \brief Eight-byte path representation of two ray cones.
///
/// Each float keeps its upper 16 bits. This preserves its range while reducing
/// the path carry-over state.
struct PackedRayFootprint {
    /// Packed pixel cone followed by lens cone.
    std::array<std::uint32_t, 2> cones{};

public:
    [[nodiscard]] KIRA_HOST_DEVICE static PackedRayFootprint
    pack(RayFootprint const &value) noexcept {
        return {
            .cones = {packCone(value.cones[0]), packCone(value.cones[1])},
        };
    }

    [[nodiscard]] KIRA_HOST_DEVICE RayFootprint unpack() const noexcept {
        return {
            .cones = {unpackCone(cones[0]), unpackCone(cones[1])},
        };
    }

private:
    [[nodiscard]] KIRA_HOST_DEVICE static std::uint32_t packCone(RayCone value) noexcept {
#if defined(__CUDA_ARCH__)
        auto const angle = __float_as_uint(value.angle);
        auto const width = __float_as_uint(value.width);
#else
        auto const angle = std::bit_cast<std::uint32_t>(value.angle);
        auto const width = std::bit_cast<std::uint32_t>(value.width);
#endif
        return (angle >> 16U) | (width & 0xffff0000U);
    }

    [[nodiscard]] KIRA_HOST_DEVICE static RayCone unpackCone(std::uint32_t value) noexcept {
        auto const angle = value << 16U;
        auto const width = value & 0xffff0000U;
#if defined(__CUDA_ARCH__)
        return {.angle = __uint_as_float(angle), .width = __uint_as_float(width)};
#else
        return {
            .angle = std::bit_cast<float>(angle),
            .width = std::bit_cast<float>(width),
        };
#endif
    }
};

static_assert(sizeof(PackedRayFootprint) == 8);
} // namespace flux
