#pragma once

#include <optix_types.h>

#include <cstdint>

#include "flux/Core/Ray.h"

namespace flux {
/// \brief Parameters shared by an OptiX intersection launch.
struct OptixLaunchParams {
    /// Geometry acceleration structure traced by the launch.
    OptixTraversableHandle traversable{};

    /// Device array of input rays.
    Ray const *rays{};

    /// Device array receiving one result per ray.
    RayHit *hits{};

    /// Number of elements in \c rays and \c hits.
    std::uint32_t rayCount{};
};
} // namespace flux
