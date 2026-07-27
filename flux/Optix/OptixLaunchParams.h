#pragma once

#include <type_traits>

#include "flux/Optix/OptixContext.h"
#include "flux/Render/Film.h"
#include "flux/Scene/Camera.h"

namespace flux {
/// \brief Parameters shared by an OptiX render launch.
struct OptixLaunchParams {
    /// Persistent device scene used by this launch.
    OptixContext::DeviceImpl scene;

    /// Launch-time camera.
    Camera::DeviceImpl camera;

    /// Launch-time output channels.
    Film::DeviceImpl film;
};

static_assert(std::is_standard_layout_v<OptixLaunchParams>);
static_assert(std::is_trivially_copyable_v<OptixLaunchParams>);
} // namespace flux
