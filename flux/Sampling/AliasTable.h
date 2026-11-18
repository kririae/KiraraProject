#pragma once

#include <cstdint>
#include <span>
#include <type_traits>

#include "kira/Compiler.h"

namespace flux {
/// \brief One bin of an alias table, which selects an index in constant time by Walker's
/// alias method.
///
/// Bin \c i of a table of \c n bins holds probability \c 1/n. It keeps the fraction
/// \c threshold of it for index \c i, and gives the rest to \c alias.
struct AliasBin {
    float threshold{};
    std::uint32_t alias{};
};

/// \brief Builds the alias table of the distribution whose cumulative weights are \p cdf.
///
/// Index \c i has the probability \f$(cdf_i - cdf_{i-1}) / cdf_{n-1}\f$, with
/// \f$cdf_{-1} = 0\f$, so an index of zero weight is never selected. An empty \p cdf gives
/// an empty table.
/// \pre \p cdf is nondecreasing and nonnegative, its last value is positive if it has one,
///      and \p bins has its size.
void buildAliasTable(std::span<float const> cdf, std::span<AliasBin> bins);

/// \brief Selects an index from the alias table \p bins of \p size bins.
///
/// Returns in \p remapped a value in \f$[0,1]\f$, uniform given the index, from the bits
/// of \p u that the selection left. Those are \f$24 - \log_2 size\f$ bits, so a table of
/// more than \f$2^{24}\f$ bins never selects some of them.
/// \pre \p size is positive.
/// \pre \p u is in \f$[0,1)\f$. Bits below \f$2^{-24}\f$ are ignored.
[[nodiscard]] KIRA_HOST_DEVICE inline std::uint32_t
sampleAliasTable(AliasBin const *bins, std::uint32_t size, float u, float &remapped) noexcept {
    // Split u into a bin and a position within it, exactly, in 24-bit fixed point.
    auto const scaled = static_cast<std::uint64_t>(u * 0x1p24F) * size;
    auto const index = static_cast<std::uint32_t>(scaled >> 24U);
    auto const position = static_cast<float>(scaled & 0xFFFFFFU) * 0x1p-24F;

    auto const bin = bins[index];
    if (position < bin.threshold) {
        remapped = position / bin.threshold;
        return index;
    }
    remapped = (position - bin.threshold) / (1.0F - bin.threshold);
    return bin.alias;
}

static_assert(std::is_standard_layout_v<AliasBin>);
static_assert(std::is_trivially_copyable_v<AliasBin>);
} // namespace flux
