#pragma once

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"

namespace flux::test {
/// \brief Makes a default path integrator and an independent sampler active in \p context.
inline void setActiveDefaults(Context &context) {
    context.setActiveIntegrator(context.create<PathIntegrator>(kira::Properties{}));
    context.setActiveSampler(context.create<IndependentSampler>(kira::Properties{}));
}
} // namespace flux::test
