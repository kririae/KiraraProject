#include "flux/Sampling/AliasTable.h"

#include <cstddef>
#include <vector>

namespace flux {
// Vose, "A linear algorithm for generating random numbers with a given distribution", IEEE TSE
// 17(9), 1991. The mass of an index is its probability times the bin count, and a full bin holds a
// mass of one. Each bin below one takes the rest of its bin from a bin above one.
void buildAliasTable(std::span<float const> cdf, std::span<AliasBin> bins) {
    if (cdf.empty())
        return;

    auto const size = cdf.size();
    auto const scale = static_cast<double>(size) / static_cast<double>(cdf.back());
    std::vector<double> masses(size);
    std::vector<std::uint32_t> smallBins;
    std::vector<std::uint32_t> largeBins;
    double previous = 0.0;
    for (std::size_t index = 0; index < size; ++index) {
        masses[index] = (static_cast<double>(cdf[index]) - previous) * scale;
        previous = cdf[index];
        (masses[index] < 1.0 ? smallBins : largeBins).push_back(static_cast<std::uint32_t>(index));
    }

    while (!smallBins.empty() && !largeBins.empty()) {
        auto const small = smallBins.back();
        smallBins.pop_back();
        auto const large = largeBins.back();

        // The large bin gives exactly what the stored threshold leaves.
        auto const threshold = static_cast<float>(masses[small]);
        bins[small] = {.threshold = threshold, .alias = large};
        masses[large] -= 1.0 - threshold;
        if (masses[large] < 1.0) {
            largeBins.pop_back();
            smallBins.push_back(large);
        }
    }

    // What remains is full up to rounding.
    for (auto const index : smallBins)
        bins[index] = {.threshold = 1.0F, .alias = index};
    for (auto const index : largeBins)
        bins[index] = {.threshold = 1.0F, .alias = index};
}
} // namespace flux
