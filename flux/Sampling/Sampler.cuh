#pragma once

#include <cstdint>

#include "flux/Sampling/Sampler.h"

namespace flux {
namespace {
KIRA_DEVICE inline std::uint64_t splitMix64(std::uint64_t &state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

KIRA_DEVICE inline std::uint32_t pcg32(std::uint64_t &state, std::uint64_t increment) noexcept {
    auto const previous = state;
    state = previous * 6364136223846793005ULL + (increment | 1ULL);
    auto const shifted = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
    auto const rotation = static_cast<std::uint32_t>(previous >> 59U);
    return (shifted >> rotation) | (shifted << ((-rotation) & 31U));
}

KIRA_DEVICE inline float toUnitFloat(std::uint32_t value) noexcept {
    return static_cast<float>((value >> 8U) * 0x1p-24F);
}
} // namespace

KIRA_DEVICE inline void IndependentSampler::DeviceImpl::startPixelSample(
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

KIRA_DEVICE inline float IndependentSampler::DeviceImpl::get1D() noexcept {
    return toUnitFloat(pcg32(state, increment));
}

KIRA_DEVICE inline Vec2f IndependentSampler::DeviceImpl::get2D() noexcept {
    return {get1D(), get1D()};
}

KIRA_DEVICE inline Vec2f IndependentSampler::DeviceImpl::getPixel2D() noexcept { return get2D(); }

KIRA_DEVICE inline void Sampler::DeviceImpl::startPixelSample(
    Vec2u const &pixel, std::uint64_t sampleIndex, Vec2u const &resolution
) noexcept {
    switch (type) {
    case SamplerType::Independent:
        return storage.independent.startPixelSample(pixel, sampleIndex, resolution);
    }
    KIRA_UNREACHABLE();
}

KIRA_DEVICE inline float Sampler::DeviceImpl::get1D() noexcept {
    switch (type) {
    case SamplerType::Independent: return storage.independent.get1D();
    }
    KIRA_UNREACHABLE();
}

KIRA_DEVICE inline Vec2f Sampler::DeviceImpl::get2D() noexcept {
    switch (type) {
    case SamplerType::Independent: return storage.independent.get2D();
    }
    KIRA_UNREACHABLE();
}

KIRA_DEVICE inline Vec2f Sampler::DeviceImpl::getPixel2D() noexcept {
    switch (type) {
    case SamplerType::Independent: return storage.independent.getPixel2D();
    }
    KIRA_UNREACHABLE();
}

namespace optix {
/// Device-side sampler dispatcher.
using Sampler = ::flux::Sampler::DeviceImpl;
} // namespace optix
} // namespace flux
