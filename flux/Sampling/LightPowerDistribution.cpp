#include "flux/Sampling/LightPowerDistribution.h"

#include "kira/Anyhow.h"

namespace flux {
std::vector<float> buildLightPowerCDF(std::span<float const> weights) {
    if (weights.size() > LightPowerDistribution::maxLightCount)
        throw kira::Anyhow("LightPowerDistribution: light count exceeds sampler resolution");

    std::vector<float> cdf;
    cdf.reserve(weights.size());
    double sum = 0.0;
    std::size_t nonzeroWeights = 0;
    for (auto const weight : weights) {
        if (weight < 0.0F)
            throw kira::Anyhow("LightPowerDistribution: estimated light power must be nonnegative");
        sum += weight;
        nonzeroWeights += weight != 0.0F;
    }

    if (weights.empty())
        return cdf;

    double cumulative = 0.0;
    if (sum == 0.0) {
        auto const probability = 1.0 / static_cast<double>(weights.size());
        for (std::size_t index = 0; index < weights.size(); ++index) {
            cumulative += probability;
            cdf.push_back(static_cast<float>(cumulative));
        }
    } else {
        constexpr double minimumProbability = 1.0 / LightPowerDistribution::maxLightCount;
        auto const remainingProbability =
            1.0 - minimumProbability * static_cast<double>(nonzeroWeights);
        for (auto const weight : weights) {
            if (weight != 0.0F)
                cumulative +=
                    minimumProbability + remainingProbability * static_cast<double>(weight) / sum;
            cdf.push_back(static_cast<float>(cumulative));
        }
    }
    return cdf;
}
} // namespace flux
