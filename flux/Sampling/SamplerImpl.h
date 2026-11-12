#pragma once

#include <cstdint>

#include "flux/Sampling/Sampler.h"

namespace flux {
namespace {
KIRA_HOST_DEVICE inline std::uint64_t splitMix64(std::uint64_t &state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

KIRA_HOST_DEVICE inline std::uint32_t
pcg32(std::uint64_t &state, std::uint64_t increment) noexcept {
    auto const previous = state;
    state = previous * 6364136223846793005ULL + (increment | 1ULL);
    auto const shifted = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
    auto const rotation = static_cast<std::uint32_t>(previous >> 59U);
    return (shifted >> rotation) | (shifted << ((-rotation) & 31U));
}

KIRA_HOST_DEVICE inline float toUnitFloat(std::uint32_t value) noexcept {
    return static_cast<float>((value >> 8U) * 0x1p-24F);
}

// The Sobol helpers below follow Cycles (src/kernel/sample/sobol_burley.h, util.h and
// util/hash.h, Copyright 2011-2022 Blender Foundation, Apache-2.0), which implements
// Burley, "Practical Hash-based Owen Scrambling", JCGT 9(4), 2020.

KIRA_HOST_DEVICE inline std::uint32_t reverseBits(std::uint32_t value) noexcept {
#if defined(__CUDA_ARCH__)
    return __brev(value);
#else
    value = ((value >> 1U) & 0x55555555U) | ((value & 0x55555555U) << 1U);
    value = ((value >> 2U) & 0x33333333U) | ((value & 0x33333333U) << 2U);
    value = ((value >> 4U) & 0x0f0f0f0fU) | ((value & 0x0f0f0f0fU) << 4U);
    value = ((value >> 8U) & 0x00ff00ffU) | ((value & 0x00ff00ffU) << 8U);
    return (value >> 16U) | (value << 16U);
#endif
}

/// Hash Prospector mixer (https://github.com/skeeto/hash-prospector); zero maps to nonzero.
KIRA_HOST_DEVICE inline std::uint32_t hashUint(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x21f0aaadU;
    value ^= value >> 15U;
    value *= 0xd35a2d97U;
    value ^= value >> 15U;
    return value ^ 0xe6fe3bebU;
}

/// \brief Base-2 Owen scramble of a bit-reversed value.
///
/// Multiplying by an even constant lets each bit affect only higher bits, so in the reversed
/// order each digit is permuted by the digits of higher significance alone, which is what makes
/// the permutation nested. This is the Laine-Karras construction with better constants
/// (https://psychopath.io/post/2021_01_30_building_a_better_lk_hash).
KIRA_HOST_DEVICE inline std::uint32_t
owenScrambleReversed(std::uint32_t value, std::uint32_t seed) noexcept {
    value ^= value * 0x3d20adeaU;
    value += seed;
    value *= (seed >> 16U) | 1U;
    value ^= value * 0x05526c56U;
    value ^= value * 0x53a22864U;
    return value;
}

/// \brief Shuffles sample \p index of a sequence with a nested uniform permutation.
///
/// The first \f$2^m\f$ indices map to one aligned block of \f$2^m\f$ indices, so the shuffled
/// sequence keeps every net property of the original. The result is below
/// \c SobolSampler::Impl::maxSamplesPerPixel.
KIRA_HOST_DEVICE inline std::uint32_t
shuffleSobolIndex(std::uint32_t index, std::uint32_t seed) noexcept {
    return reverseBits(owenScrambleReversed(reverseBits(index), seed)) &
           (SobolSampler::Impl::maxSamplesPerPixel - 1U);
}

/// \brief Returns dimension \p Dim of Sobol point \p index, Owen-scrambled with \p seed.
///
/// \pre \p index is below \c SobolSampler::Impl::maxSamplesPerPixel.
template <int Dim>
KIRA_HOST_DEVICE inline float sobolSample(std::uint32_t index, std::uint32_t seed) noexcept {
    // Direction numbers with reversed bits, first 16 of each dimension. Dimension 0 is the
    // identity, so its reversed Sobol value is the index itself.
    constexpr std::uint32_t directions[2][16] = {
        {0x0001U, 0x0003U, 0x0005U, 0x000fU, 0x0011U, 0x0033U, 0x0055U, 0x00ffU, 0x0101U, 0x0303U,
         0x0505U, 0x0f0fU, 0x1111U, 0x3333U, 0x5555U, 0xffffU},
        {0x0001U, 0x0003U, 0x0006U, 0x0009U, 0x0017U, 0x003aU, 0x0071U, 0x00a3U, 0x0116U, 0x0339U,
         0x0677U, 0x09aaU, 0x1601U, 0x3903U, 0x7706U, 0xaa09U},
    };
    auto reversed = index;
    if constexpr (Dim > 0) {
        reversed = 0;
#if defined(__CUDA_ARCH__)
#pragma unroll
#endif
        for (int bit = 0; bit < 16; ++bit)
            reversed ^= ((index >> bit) & 1U) != 0U ? directions[Dim - 1][bit] : 0U;
    }
    return toUnitFloat(reverseBits(owenScrambleReversed(reversed, seed)));
}

/// \brief Returns the seed of the dimension set drawn for \p use at \p depth.
KIRA_HOST_DEVICE inline std::uint32_t
sobolSetSeed(std::uint32_t pixelSeed, SampleUse use, std::uint32_t depth) noexcept {
    return pixelSeed ^ hashUint(depth * sampleUseCount + static_cast<std::uint32_t>(use));
}
} // namespace

KIRA_HOST_DEVICE inline void IndependentSampler::Impl::startPixelSample(
    Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
) noexcept {
    auto const pixelIndex = static_cast<std::uint64_t>(pixel.y()) * resolution.x() + pixel.x();
    std::uint64_t seed = sampleIndex * 0x9e3779b97f4a7c15ULL + pixelIndex;
    state = 0;
    increment = (splitMix64(seed) << 1U) | 1ULL;
    (void)pcg32(state, increment);
    state += splitMix64(seed);
    (void)pcg32(state, increment);
}

KIRA_HOST_DEVICE inline float IndependentSampler::Impl::get1D(SampleUse, std::uint32_t) noexcept {
    return toUnitFloat(pcg32(state, increment));
}

// Braced initialization evaluates left to right, which fixes the order of the draws.
KIRA_HOST_DEVICE inline Vec2f
IndependentSampler::Impl::get2D(SampleUse use, std::uint32_t depth) noexcept {
    return {get1D(use, depth), get1D(use, depth)};
}

KIRA_HOST_DEVICE inline Vec3f
IndependentSampler::Impl::get3D(SampleUse use, std::uint32_t depth) noexcept {
    return {get1D(use, depth), get1D(use, depth), get1D(use, depth)};
}

KIRA_HOST_DEVICE inline void SobolSampler::Impl::startPixelSample(
    Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
) noexcept {
    index = static_cast<std::uint32_t>(sampleIndex);
    seed = hashUint(pixel.y() * resolution.x() + pixel.x());
}

// Each call shuffles the index with its own seed and scrambles each coordinate with another,
// so the 1D, 2D and 3D sets drawn from one seed are uncorrelated with one another. The
// constants are the ones Cycles uses.
KIRA_HOST_DEVICE inline float
SobolSampler::Impl::get1D(SampleUse use, std::uint32_t depth) noexcept {
    auto const set = sobolSetSeed(seed, use, depth);
    auto const i = shuffleSobolIndex(index, set ^ 0xbff95bfeU);
    return sobolSample<0>(i, set ^ 0x635c77bdU);
}

KIRA_HOST_DEVICE inline Vec2f
SobolSampler::Impl::get2D(SampleUse use, std::uint32_t depth) noexcept {
    auto const set = sobolSetSeed(seed, use, depth);
    auto const i = shuffleSobolIndex(index, set ^ 0xf8ade99aU);
    return {sobolSample<0>(i, set ^ 0xe0aaaf76U), sobolSample<1>(i, set ^ 0x94964d4eU)};
}

KIRA_HOST_DEVICE inline Vec3f
SobolSampler::Impl::get3D(SampleUse use, std::uint32_t depth) noexcept {
    auto const set = sobolSetSeed(seed, use, depth);
    auto const i = shuffleSobolIndex(index, set ^ 0xcaa726acU);
    return {
        sobolSample<0>(i, set ^ 0x9e78e391U),
        sobolSample<1>(i, set ^ 0x67c33241U),
        sobolSample<2>(i, set ^ 0x78c395c5U),
    };
}

KIRA_HOST_DEVICE inline void Sampler::Impl::startPixelSample(
    Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
) noexcept {
    dispatch([&](auto &sampler) { sampler.startPixelSample(pixel, sampleIndex, resolution); });
}

KIRA_HOST_DEVICE inline float Sampler::Impl::get1D(SampleUse use, std::uint32_t depth) noexcept {
    return dispatch([&](auto &sampler) { return sampler.get1D(use, depth); });
}

KIRA_HOST_DEVICE inline Vec2f Sampler::Impl::get2D(SampleUse use, std::uint32_t depth) noexcept {
    return dispatch([&](auto &sampler) { return sampler.get2D(use, depth); });
}

KIRA_HOST_DEVICE inline Vec3f Sampler::Impl::get3D(SampleUse use, std::uint32_t depth) noexcept {
    return dispatch([&](auto &sampler) { return sampler.get3D(use, depth); });
}

namespace optix {
/// Sampler used by OptiX device programs.
using Sampler = ::flux::Sampler::Impl;
} // namespace optix
} // namespace flux
