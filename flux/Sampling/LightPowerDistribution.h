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
/// \brief Selects lights from a power-weighted distribution.
struct LightPowerDistribution {
    /// Largest light table addressed without exceeding sampler precision.
    static constexpr std::uint32_t maxLightCount = 1U << 24U;
    static constexpr std::uint32_t invalidSlot = std::numeric_limits<std::uint32_t>::max();

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
    [[nodiscard]] KIRA_HOST_DEVICE std::uint32_t sample(float u, float &pmfValue) const noexcept {
        if (numLights == 0) {
            pmfValue = 0.0F;
            return invalidSlot;
        }
        if (numLights == 1) {
            pmfValue = 1.0F;
            return 0;
        }

        auto target = u * sum;
        if (!(target < sum))
            target = std::nextafter(sum, 0.0F);
        auto const index = static_cast<std::uint32_t>(upperBoundIndex(cdf, numLights, target));
        pmfValue = pmf(index);
        return index;
    }

    /// \brief Returns the selection probability of \p slot.
    ///
    /// Returns zero when \p slot is outside the distribution.
    [[nodiscard]] KIRA_HOST_DEVICE float pmf(std::uint32_t slot) const noexcept {
        if (slot >= numLights)
            return 0.0F;
        if (numLights == 1)
            return 1.0F;
        auto const prev = slot == 0 ? 0.0F : cdf[slot - 1];
        return (cdf[slot] - prev) / sum;
    }
};

/// \brief Builds a normalized power CDF.
///
/// Each positive weight receives a probability representable by a 24-bit
/// uniform sample. Nonpositive weights receive zero probability. When no
/// weight is positive, all lights receive equal probability.
/// \pre \p weights contains at most \c LightPowerDistribution::maxLightCount elements.
[[nodiscard]] std::vector<float> buildLightPowerCDF(std::span<float const> weights);

static_assert(std::is_standard_layout_v<LightPowerDistribution>);
static_assert(std::is_trivially_copyable_v<LightPowerDistribution>);
} // namespace flux
