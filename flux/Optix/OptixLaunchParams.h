#pragma once

#include <cstdint>
#include <type_traits>

#include "flux/Optix/OptixContext.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

namespace flux {
/// \brief Parameters shared by an OptiX render launch.
struct OptixLaunchParams {
    /// Persistent device scene used by this launch.
    OptixContext::DeviceImpl scene;

    /// Launch-time camera.
    Camera::DeviceImpl camera;

    /// Launch-time sampler dispatcher.
    Sampler::DeviceImpl sampler;

    /// Sample sequence index for this launch.
    std::uint64_t sampleIndex;

    /// Launch-time output channels.
    Film::DeviceImpl film;
};

static_assert(std::is_standard_layout_v<OptixLaunchParams>);
static_assert(std::is_trivially_copyable_v<OptixLaunchParams>);
} // namespace flux
