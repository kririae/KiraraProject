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

KIRA_HOST_DEVICE inline float IndependentSampler::Impl::get1D() noexcept {
    return toUnitFloat(pcg32(state, increment));
}

KIRA_HOST_DEVICE inline Vec2f IndependentSampler::Impl::get2D() noexcept {
    return {get1D(), get1D()};
}

KIRA_HOST_DEVICE inline Vec2f IndependentSampler::Impl::getPixel2D() noexcept { return get2D(); }

KIRA_HOST_DEVICE inline void Sampler::Impl::startPixelSample(
    Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
) noexcept {
    dispatch([&](auto &sampler) { sampler.startPixelSample(pixel, sampleIndex, resolution); });
}

KIRA_HOST_DEVICE inline float Sampler::Impl::get1D() noexcept {
    return dispatch([](auto &sampler) { return sampler.get1D(); });
}

KIRA_HOST_DEVICE inline Vec2f Sampler::Impl::get2D() noexcept {
    return dispatch([](auto &sampler) { return sampler.get2D(); });
}

KIRA_HOST_DEVICE inline Vec2f Sampler::Impl::getPixel2D() noexcept {
    return dispatch([](auto &sampler) { return sampler.getPixel2D(); });
}

namespace optix {
/// Sampler used by OptiX device programs.
using Sampler = ::flux::Sampler::Impl;
} // namespace optix
} // namespace flux
