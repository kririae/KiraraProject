#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "flux/Scene/Light.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief One light selected from a backend light table.
///
/// A zero PMF marks an empty selection. Light indices are dense only within
/// the backend sync that produced the sampler.
struct SampledLight {
    /// Sentinel used when no light can be selected.
    static constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();

    /// Global index in LightSampler::lights.
    std::uint32_t lightIndex{invalidIndex};

    /// Discrete probability of selecting \c lightIndex.
    float pmf{};
};

/// \brief Uniform light-selection view used by shared transport code.
///
/// Renderer backends own the light table and any persistent sampling data.
/// This trivially copyable view borrows that state for one render.
struct LightSampler {
    /// Largest light table addressed without exceeding sampler precision.
    static constexpr std::uint32_t maxLightCount = 1U << 24U;

    /// Lights selected and sampled through this view.
    LightTable lights;

public:
    /// \brief Selects one light with uniform probability.
    ///
    /// \param surface Current shading point.
    /// \param sample Uniform value in the half-open unit interval.
    /// \pre \c lights.numLights is at most \c maxLightCount.
    [[nodiscard]] KIRA_HOST_DEVICE SampledLight
    sample(SurfaceInteraction const &surface, float sample) const noexcept {
        (void)surface;
        if (lights.numLights == 0)
            return {};

        auto const index = std::min(
            static_cast<std::uint32_t>(sample * static_cast<float>(lights.numLights)),
            lights.numLights - 1
        );
        return {
            .lightIndex = index,
            .pmf = 1.0F / static_cast<float>(lights.numLights),
        };
    }

    /// \brief Returns the uniform selection probability of \p lightIndex.
    [[nodiscard]] KIRA_HOST_DEVICE float
    pmf(SurfaceInteraction const &surface, std::uint32_t lightIndex) const noexcept {
        (void)surface;
        return lightIndex < lights.numLights ? 1.0F / static_cast<float>(lights.numLights) : 0.0F;
    }

    /// \brief Samples incident radiance from one selected light.
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample sampleDirect(
        std::uint32_t lightIndex, SurfaceInteraction const &surface, Vec2f const &sample
    ) const noexcept {
        return lights.sampleDirect(lightIndex, surface, sample);
    }
};

static_assert(std::is_standard_layout_v<SampledLight>);
static_assert(std::is_trivially_copyable_v<SampledLight>);
static_assert(std::is_standard_layout_v<LightSampler>);
static_assert(std::is_trivially_copyable_v<LightSampler>);
} // namespace flux
