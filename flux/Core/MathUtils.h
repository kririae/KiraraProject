#pragma once

/// \file flux/Core/MathUtils.h
/// \brief Small sampling transforms shared by host and device code.

#include <cmath>
#include <numbers>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Maps a uniform square sample to a uniform point on the unit disk.
///
/// \param sample Point in the half-open unit square.
/// \return Point inside the unit disk.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec2f uniformSampleDisk(Vec2f const &sample) noexcept {
    auto const offset = sample * 2.0F - Vec2f{1.0F, 1.0F};
    if (offset.x() == 0.0F && offset.y() == 0.0F)
        return {};

    constexpr auto piOverFour = std::numbers::pi_v<float> * 0.25F;
    constexpr auto piOverTwo = std::numbers::pi_v<float> * 0.5F;
    float radius;
    float angle;
    if (std::abs(offset.x()) > std::abs(offset.y())) {
        radius = offset.x();
        angle = piOverFour * offset.y() / offset.x();
    } else {
        radius = offset.y();
        angle = piOverTwo - piOverFour * offset.x() / offset.y();
    }
    return Vec2f{std::cos(angle), std::sin(angle)} * radius;
}
} // namespace flux
