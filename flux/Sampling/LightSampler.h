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
    /// Dense index in LightSampler::lights.
    std::uint32_t lightIndex{invalidIndex};
    /// Discrete probability of selecting \c lightIndex.
    float pmf{};
};

/// \brief Uniform light-selection view used by shared transport code.
///
/// Renderer backends own the light table and any persistent sampling data.
/// This view remains valid until the owning backend rebuilds or destroys its
/// light table.
struct LightSampler {
    /// Largest light table addressed without exceeding sampler precision.
    static constexpr std::uint32_t maxLightCount = 1U << 24U;

    /// Light table borrowed from the owning renderer backend.
    LightTable lights;

public:
    /// \brief Selects one light for \p context with uniform probability.
    ///
    /// An empty light table returns an invalid selection.
    /// \pre \p sample is in \f$[0,1)\f$.
    /// \pre \c lights.numLights is at most \c maxLightCount.
    [[nodiscard]] KIRA_HOST_DEVICE SampledLight
    sample(LightSamplingContext const &context, float sample) const noexcept {
        (void)context;
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
    ///
    /// Returns zero when \p lightIndex is outside the light table.
    [[nodiscard]] KIRA_HOST_DEVICE float
    pmf(LightSamplingContext const &context, std::uint32_t lightIndex) const noexcept {
        (void)context;
        return lightIndex < lights.numLights ? 1.0F / static_cast<float>(lights.numLights) : 0.0F;
    }

    /// \brief Samples incident radiance from one selected light.
    ///
    /// \pre \p lightIndex is less than `lights.numLights`.
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample sampleDirect(
        std::uint32_t lightIndex, LightSamplingContext const &context, Vec2f const &sample
    ) const noexcept {
        return lights.sampleDirect(lightIndex, context, sample);
    }
};

static_assert(std::is_standard_layout_v<SampledLight>);
static_assert(std::is_trivially_copyable_v<SampledLight>);
static_assert(std::is_standard_layout_v<LightSampler>);
static_assert(std::is_trivially_copyable_v<LightSampler>);
} // namespace flux
