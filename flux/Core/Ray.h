#pragma once

#include <cstdint>
#include <limits>

#include "flux/Core/Math.h"

namespace flux {
/// \brief Selects one SBT ray path.
enum class RayType : std::uint32_t { // NOLINT
    Radiance,
    Shadow,
    Count,
};

/// \brief Ray traced by the renderer.
struct Ray {
    /// Ray origin in world space.
    Vec3f origin;

    /// *Normalized* ray direction in world space.
    Vec3f direction;

    /// Minimum trace distance.
    float minDistance{0.0F};

    /// Maximum trace distance.
    float maxDistance{std::numeric_limits<float>::max()};
};
} // namespace flux
