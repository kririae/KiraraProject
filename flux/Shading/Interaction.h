#pragma once

#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/Primitive.h"

namespace flux {
/// \brief Geometric information at a ray-surface intersection.
///
/// Positions and normals use world space.
struct SurfaceInteraction {
    /// World-space position at the intersection.
    Vec3f position{};

    /// World-space normal derived from the underlying geometry.
    ///
    /// Material initialization does not modify this normal.
    Vec3f geometricNormal{};

    /// World-space normal used for shading.
    ///
    /// Geometry initializes this normal; material initialization may replace it.
    Vec3f shadingNormal{};

    /// Surface parameterization, or zero when the geometry has no texture coordinates.
    Vec2f uv{};

    /// Primitive in the current device scene.
    Primitive::DeviceImpl const *primitive{};

    /// Element index within the primitive's geometry.
    std::uint32_t elementIndex{};

    /// Ray distance at the intersection.
    float distance{};
};

static_assert(std::is_standard_layout_v<SurfaceInteraction>);
static_assert(std::is_trivially_copyable_v<SurfaceInteraction>);
} // namespace flux
