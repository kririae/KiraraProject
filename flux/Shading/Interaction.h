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
    Vec3f geometricNormal{};

    /// World-space normal used for shading.
    Vec3f shadingNormal{};

    /// Primitive in the current device scene.
    Primitive::DeviceImpl const *primitive{};

    /// Element index within the primitive's geometry.
    std::uint32_t primitiveIndex{};

    /// Ray distance at the intersection.
    float distance{};
};

static_assert(std::is_standard_layout_v<SurfaceInteraction>);
static_assert(std::is_trivially_copyable_v<SurfaceInteraction>);
} // namespace flux
