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
    /// \brief Spawns a visibility ray toward \p target.
    ///
    /// The origin moves along the geometric normal on the target-facing side.
    /// The maximum distance stops before the target to avoid reporting either
    /// endpoint as an occluder.
    [[nodiscard]] KIRA_HOST_DEVICE Ray spawnRayTo(Vec3f const &target) const noexcept {
        constexpr float originEpsilon = 1.0e-5F;
        constexpr float targetEpsilon = 1.0e-6F;

        auto const initialDirection = target - position;
        auto const initialSquaredDistance = initialDirection.norm2();
        if (!(initialSquaredDistance > 0.0F))
            return {.origin = position, .direction = {}, .maxDistance = 0.0F};

        auto const normal =
            initialDirection.dot(geometricNormal) >= 0.0F ? geometricNormal : -geometricNormal;
        auto const magnitude = 1.0F + std::max(
                                          std::abs(position.x()),
                                          std::max(std::abs(position.y()), std::abs(position.z()))
                                      );
        auto const initialDistance = std::sqrt(initialSquaredDistance);
        auto const offsetDistance = std::min(magnitude * originEpsilon, initialDistance * 0.5F);
        auto const origin = position + normal * offsetDistance;
        auto const offset = target - origin;
        auto const squaredDistance = offset.norm2();
        if (!(squaredDistance > 0.0F))
            return {.origin = origin, .direction = {}, .maxDistance = 0.0F};

        auto const rayDistance = std::sqrt(squaredDistance);
        return {
            .origin = origin,
            .direction = offset / rayDistance,
            .maxDistance = rayDistance * (1.0F - targetEpsilon),
        };
    }
};

static_assert(std::is_standard_layout_v<SurfaceInteraction>);
static_assert(std::is_trivially_copyable_v<SurfaceInteraction>);
} // namespace flux
