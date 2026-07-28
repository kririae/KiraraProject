#pragma once

#include <type_traits>

#include "flux/Core/Ray.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Mutable state carried by one path sample.
struct PathState {
    /// Ray for the next traversal.
    Ray ray;

    /// Whether another path vertex should be processed.
    bool active{true};
};

/// \brief Device-side path-integrator operations.
///
/// The megakernel owns scheduling. These operations only update one path.
struct PathIntegrator::DeviceImpl {
    /// \brief Terminates a path that escaped the scene.
    KIRA_DEVICE void onMiss(PathState &state) const noexcept { state.active = false; }

    /// \brief Terminates the path at its first surface hit.
    ///
    /// \post \p state is inactive.
    KIRA_DEVICE void
    onSurfaceHit(PathState &state, SurfaceInteraction const &surface) const noexcept {
        (void)surface;
        state.active = false;
    }
};

static_assert(std::is_standard_layout_v<PathState>);
static_assert(std::is_trivially_copyable_v<PathState>);
} // namespace flux
