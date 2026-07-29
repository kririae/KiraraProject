#pragma once

#include <type_traits>

#include "flux/Core/Ray.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Mutable state carried by one path sample.
struct PathState {
    /// Ray for the next traversal.
    Ray ray;

    /// Sampling sequence advanced across path vertices.
    Sampler::Impl sampler;

    /// Number of surface bounces already processed.
    std::uint32_t bounce{};

    /// Whether another path vertex should be processed.
    bool active{true};
};

/// \brief Operations that advance one path.
///
/// A renderer backend owns scheduling. These operations only update one path.
struct PathIntegrator::Impl {
    /// \brief Terminates a path that escaped the scene.
    KIRA_HOST_DEVICE void onMiss(PathState &state) const noexcept { state.active = false; }

    /// \brief Terminates the path at its first surface hit.
    ///
    /// \post \p state is inactive.
    KIRA_HOST_DEVICE void
    onSurfaceHit(PathState &state, SurfaceInteraction const &surface) const noexcept {
        (void)surface;
        state.active = false;
    }
};

static_assert(std::is_standard_layout_v<PathState>);
static_assert(std::is_trivially_copyable_v<PathState>);
} // namespace flux
