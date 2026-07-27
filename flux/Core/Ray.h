#pragma once

#include <cstdint>
#include <limits>

#include "flux/Core/Math.h"

namespace flux {
/// \brief Ray passed to an intersection query.
struct Ray {
    /// Ray origin in world space.
    Vec3f origin;

    /// Normalized ray direction in world space.
    Vec3f direction;

    /// Minimum trace distance.
    float minDistance{0.0F};

    /// Maximum trace distance.
    float maxDistance{std::numeric_limits<float>::max()};
};

/// \brief Result of intersecting one ray with the current scene.
///
/// Geometric fields are meaningful only when \c isHit returns true.
struct RayHit {
    /// Distance from the ray origin to the closest hit.
    float distance{};

    /// Triangle index within the intersected mesh.
    std::uint32_t primitiveIndex{};

    /// Mesh index in the device scene's context order.
    std::uint32_t geometryIndex{};

    /// Nonzero when the ray found a surface.
    std::uint32_t hit{};

    /// \brief Returns whether the ray found a surface.
    [[nodiscard]] constexpr bool isHit() const noexcept { return hit != 0; }
};
} // namespace flux
