#pragma once

#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"

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
};

static_assert(std::is_standard_layout_v<SurfaceInteraction>);
static_assert(std::is_trivially_copyable_v<SurfaceInteraction>);
} // namespace flux
