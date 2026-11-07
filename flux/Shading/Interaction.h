#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Core/Ray.h"
#include "flux/Core/RayFootprint.h"
#include "flux/Shading/Frame.h"
#include "flux/Shading/Texture.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Geometric information at a ray-surface intersection.
///
/// Positions and normals use world space.
struct SurfaceInteraction {
    /// World-space position at the intersection.
    Vec3f position{};

    /// World-space normal defined by Geometry.
    Vec3f geometricNormal{};

    /// World-space normal used for shading.
    Vec3f shadingNormal{};

    /// Surface parameterization; zero is the fallback value.
    Vec2f uv{};

    /// Gradient of \c uv.x() with respect to world-space position, or zero without texture
    /// coordinates.
    ///
    /// A world-space offset \p dp maps to a \c uv offset of
    /// \c {dp.dot(uvGradU), dp.dot(uvGradV)}.
    Vec3f uvGradU{};

    /// Gradient of \c uv.y() with respect to world-space position, or zero without texture
    /// coordinates.
    Vec3f uvGradV{};

    /// Primitive index assigned by Context.
    std::uint32_t primitiveIndex{};

    /// Element index within the primitive's geometry.
    std::uint32_t elementIndex{};

public:
    /// \brief Spawns a semi-infinite ray along \p direction.
    [[nodiscard]] KIRA_HOST_DEVICE Ray spawnRay(Vec3f const &direction) const noexcept {
        return {
            .origin = offsetRayOrigin(position, geometricNormal, direction),
            .direction = direction,
        };
    }

    /// \brief Spawns a visibility ray toward \p target.
    ///
    /// The maximum distance stops before the target. A coincident target
    /// produces an invalid ray with zero maximum distance.
    [[nodiscard]] KIRA_HOST_DEVICE Ray spawnRayTo(Vec3f const &target) const noexcept {
        auto const initialD = target - position;
        if (initialD.norm2() == 0.0F)
            return {.origin = position, .direction = {}, .maxDistance = 0.0F};

        auto origin = offsetRayOrigin(position, geometricNormal, initialD);
        auto d = target - origin;
        if (d.dot(initialD) <= 0.0F) {
            origin = position + initialD * 0.5F;
            d = target - origin;
        }
        auto const dist2 = d.norm2();
        if (dist2 == 0.0F)
            return {.origin = origin, .direction = {}, .maxDistance = 0.0F};

        auto const dist = std::sqrt(dist2);
        return {
            .origin = origin,
            .direction = d / dist,
            .maxDistance = dist * (1.0F - shadowEpsilon),
        };
    }
};

static_assert(std::is_standard_layout_v<SurfaceInteraction>);
static_assert(std::is_trivially_copyable_v<SurfaceInteraction>);

/// \brief The surface point that a BSDF is executed at.
struct ShadingContext {
    Vec3f position{};

    /// The shading normal, which determines the shading frame as in \c Frame.
    Vec3f normal{};

    /// The unit vector orthogonal to \c normal in which \c uv.x() grows.
    Vec3f tangent{};

    TextureEvalContext texture{};
};

/// \brief Returns the tangent of the shading context at a surface point.
///
/// \pre \p normal is normalized.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
computeShadingTangent(Vec3f const &normal, Vec3f const &uvGradU) noexcept {
    constexpr auto degenerate = 1.0e-10F;
    auto const projected = uvGradU - normal * uvGradU.dot(normal);
    auto const length2 = projected.norm2();
    if (length2 > degenerate * uvGradU.norm2())
        return projected / std::sqrt(length2);
    return Frame{normal}.getTangent();
}

/// \brief Returns the shading context at \p isect.
///
/// \p footprint is projected to the plane of \p isect.
[[nodiscard]] KIRA_HOST_DEVICE inline ShadingContext makeShadingContext(
    SurfaceInteraction const &isect, Vec3f const &direction, RayFootprint const &footprint
) noexcept {
    auto dpdx = Vec3f{};
    auto dpdy = Vec3f{};
    footprint.project(direction, isect.geometricNormal, dpdx, dpdy);
    return {
        .position = isect.position,
        .normal = isect.shadingNormal,
        .tangent = computeShadingTangent(isect.shadingNormal, isect.uvGradU),
        .texture = {
            .uv = isect.uv,
            .duvdx = {dpdx.dot(isect.uvGradU), dpdx.dot(isect.uvGradV)},
            .duvdy = {dpdy.dot(isect.uvGradU), dpdy.dot(isect.uvGradV)},
        },
    };
}

static_assert(std::is_standard_layout_v<ShadingContext>);
static_assert(std::is_trivially_copyable_v<ShadingContext>);
} // namespace flux
