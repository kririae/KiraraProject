#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Core/Ray.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Geometric information at a ray-surface intersection.
///
/// Positions and normals use world space.
struct SurfaceInteraction {
    /// World-space position at the intersection.
    Vec3f position{};

    /// World-space normal defined by Geometry.
    ///
    /// BSDF::init() preserves this normal.
    Vec3f geometricNormal{};

    /// World-space normal used for shading.
    ///
    /// Geometry initializes this normal; BSDF::init() may replace it.
    Vec3f shadingNormal{};

    /// Surface parameterization, or zero when the geometry has no texture coordinates.
    Vec2f uv{};

    /// Dense primitive index in the current backend scene.
    std::uint32_t primitiveIndex{};

    /// Element index within the primitive's geometry.
    std::uint32_t elementIndex{};

    /// Ray distance at the intersection.
    float distance{};

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
} // namespace flux
