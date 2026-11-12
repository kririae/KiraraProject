#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
inline constexpr float shadowEpsilon = 1.0e-6F;

/// \brief Ray traced by the renderer.
struct Ray {
    /// Ray origin in world space.
    Vec3f origin;

    /// *Normalized* ray direction in world space.
    Vec3f direction;

    /// Minimum trace distance.
    float minDistance{0.0F};

    /// Maximum trace distance.
    float maxDistance{std::numeric_limits<float>::max()};
};

/// \brief Offsets a surface point to avoid self-intersection.
///
/// The normal is oriented toward \p d. The integer offset follows
/// "A Fast and Robust Method for Avoiding Self-Intersection" by Wächter and
/// Binder.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
offsetRayOrigin(Vec3f const &p, Vec3f const &n, Vec3f const &d) noexcept {
    constexpr float origin = 1.0F / 32.0F;
    constexpr float floatScale = 1.0F / 65536.0F;
    constexpr float intScale = 256.0F;

    auto const fn = d.dot(n) >= 0.0F ? n : -n;
    auto po = p;
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(p[i]) < origin) {
            po[i] = p[i] + floatScale * fn[i];
            continue;
        }

        auto const offset = static_cast<std::int32_t>(intScale * fn[i]);
#if defined(__CUDA_ARCH__)
        auto const bits = __float_as_int(p[i]);
        po[i] = __int_as_float(bits + (p[i] < 0.0F ? -offset : offset));
#else
        auto const bits = std::bit_cast<std::int32_t>(p[i]);
        po[i] = std::bit_cast<float>(bits + (p[i] < 0.0F ? -offset : offset));
#endif
    }
    return po;
}
} // namespace flux
