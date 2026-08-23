#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "flux/Core/MathUtils.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief A selected light and its probability.
///
/// A zero PMF marks no selection.
struct SampledLight {
    /// Sentinel used when no light can be selected.
    static constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();
    /// Dense light index.
    std::uint32_t lightIndex{invalidIndex};
    /// Discrete probability of selecting \c lightIndex.
    float pmf{};
};

/// \brief Selects lights from a power-weighted distribution.
struct LightPowerDistribution {
    /// Largest light table addressed without exceeding sampler precision.
    static constexpr std::uint32_t maxLightCount = 1U << 24U;

    /// Cumulative selection weights.
    float const *cdf{};
    /// Final value of \c cdf.
    float sum{};
    /// Number of selectable lights.
    std::uint32_t numLights{};

public:
    /// \brief Selects one light by estimated power.
    ///
    /// An empty sampler returns an invalid selection.
    /// \pre \p u is in \f$[0,1)\f$.
    /// \pre \c numLights is at most \c maxLightCount.
    [[nodiscard]] KIRA_HOST_DEVICE SampledLight sample(float u) const noexcept {
        if (numLights == 0)
            return {};
        if (numLights == 1)
            return {.lightIndex = 0, .pmf = 1.0F};

        auto target = u * sum;
        if (!(target < sum))
            target = std::nextafter(sum, 0.0F);
        auto const index = static_cast<std::uint32_t>(upperBoundIndex(cdf, numLights, target));
        return {
            .lightIndex = index,
            .pmf = pmf(index),
        };
    }

    /// \brief Returns the selection probability of \p lightIndex.
    ///
    /// Returns zero when \p lightIndex is outside the light table.
    [[nodiscard]] KIRA_HOST_DEVICE float pmf(std::uint32_t lightIndex) const noexcept {
        if (lightIndex >= numLights)
            return 0.0F;
        if (numLights == 1)
            return 1.0F;
        auto const prev = lightIndex == 0 ? 0.0F : cdf[lightIndex - 1];
        return (cdf[lightIndex] - prev) / sum;
    }
};

/// \brief Builds a normalized power CDF.
///
/// Each positive weight receives a probability representable by a 24-bit
/// uniform sample.
[[nodiscard]] std::vector<float> buildLightPowerCDF(std::span<float const> weights);

static_assert(std::is_standard_layout_v<SampledLight>);
static_assert(std::is_trivially_copyable_v<SampledLight>);
static_assert(std::is_standard_layout_v<LightPowerDistribution>);
static_assert(std::is_trivially_copyable_v<LightPowerDistribution>);
} // namespace flux
