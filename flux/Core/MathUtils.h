#pragma once

/// \file flux/Core/MathUtils.h
/// \brief Small math utilities shared by host and device code.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Returns the first index whose value is greater than \p value.
///
/// \pre \p values addresses \p size elements sorted in ascending order.
/// \return \p size when no such element exists.
template <typename T, typename U>
[[nodiscard]] KIRA_HOST_DEVICE inline std::size_t
upperBoundIndex(T const *values, std::size_t size, U const &value) noexcept {
    if (size == 0)
        return 0;

    auto length = size;
    std::size_t begin = 0;
#if defined(__CUDA_ARCH__)
    constexpr auto numBits = sizeof(unsigned long long) * 8;
    auto step = std::size_t{1} << (numBits - __clzll(length) - 1);
#else
    auto step = std::bit_floor(length);
#endif

    // Shar's branchless binary search handles a non-power-of-two tail first.
    // https://probablydance.com/2023/04/27/beautiful-branchless-binary-search/
    if (step != length && !(value < values[step])) {
        length -= step + 1;
        if (length == 0)
            return size;
#if defined(__CUDA_ARCH__)
        step = length == 1 ? 1 : std::size_t{1} << (numBits - __clzll(length - 1));
#else
        step = std::bit_ceil(length);
#endif
        begin = size - step;
    }
    for (step /= 2; step != 0; step /= 2)
        if (!(value < values[begin + step]))
            begin += step;

    return begin + static_cast<std::size_t>(!(value < values[begin]));
}

/// \brief Maps one uniform variate to triangle barycentric coordinates.
///
/// Uses the Basu-Owen recursive triangle subdivision.
/// \pre \p sample is in \f$[0,1)\f$.
/// \see https://pharr.org/matt/blog/2019/02/27/triangle-sampling-1
[[nodiscard]] KIRA_HOST_DEVICE inline Vec2f lowDiscrepancySampleTriangle(float sample) noexcept {
    auto fixed = static_cast<std::uint32_t>(sample * 4294967296.0F);
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.5F;

#if defined(__CUDA_ARCH__)
#pragma unroll 8
#endif
    for (std::uint32_t index = 0; index < 16; ++index) {
        auto const bits = fixed >> 30;
        auto const flip = (bits & 3U) == 0;
        y += ((bits & 1U) == 0) * width;
        x += ((bits & 2U) == 0) * width;
        width *= flip ? -0.5F : 0.5F;
        fixed <<= 2;
    }
    return {x + width / 3.0F, y + width / 3.0F};
}

[[nodiscard]] KIRA_HOST_DEVICE constexpr float luminance(Spectrum const &value) noexcept {
    return 0.2126F * value.x() + 0.7152F * value.y() + 0.0722F * value.z();
}

/// \brief Returns the cross product of two three-dimensional vectors.
[[nodiscard]] KIRA_HOST_DEVICE constexpr Vec3f cross(Vec3f const &lhs, Vec3f const &rhs) noexcept {
    return {
        lhs.y() * rhs.z() - lhs.z() * rhs.y(),
        lhs.z() * rhs.x() - lhs.x() * rhs.z(),
        lhs.x() * rhs.y() - lhs.y() * rhs.x(),
    };
}

/// \brief Transforms a point by a row-major 3x4 affine transform.
///
/// \pre \p transform addresses 12 floats.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
transformPoint(float const *transform, Vec3f const &point) noexcept {
    return {
        transform[0] * point.x() + transform[1] * point.y() + transform[2] * point.z() +
            transform[3],
        transform[4] * point.x() + transform[5] * point.y() + transform[6] * point.z() +
            transform[7],
        transform[8] * point.x() + transform[9] * point.y() + transform[10] * point.z() +
            transform[11],
    };
}

/// \brief Transforms a point by three affine-transform rows.
template <typename Row>
    requires requires(Row const &row) {
        row.x;
        row.y;
        row.z;
        row.w;
    }
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
transformPoint(Row const *transform, Vec3f const &point) noexcept {
    return {
        transform[0].x * point.x() + transform[0].y * point.y() + transform[0].z * point.z() +
            transform[0].w,
        transform[1].x * point.x() + transform[1].y * point.y() + transform[1].z * point.z() +
            transform[1].w,
        transform[2].x * point.x() + transform[2].y * point.y() + transform[2].z * point.z() +
            transform[2].w,
    };
}

/// \brief Transforms a vector by a row-major 3x3 transform.
///
/// \pre \p transform addresses 9 floats.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
transformVec(float const *transform, Vec3f const &vector) noexcept {
    return {
        transform[0] * vector.x() + transform[1] * vector.y() + transform[2] * vector.z(),
        transform[3] * vector.x() + transform[4] * vector.y() + transform[5] * vector.z(),
        transform[6] * vector.x() + transform[7] * vector.y() + transform[8] * vector.z(),
    };
}

/// \brief Multiplies a vector by the transpose of three transform rows.
template <typename Row>
    requires requires(Row const &row) {
        row.x;
        row.y;
        row.z;
    }
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f
transformTransposeVec(Row const *transform, Vec3f const &vector) noexcept {
    return {
        transform[0].x * vector.x() + transform[1].x * vector.y() + transform[2].x * vector.z(),
        transform[0].y * vector.x() + transform[1].y * vector.y() + transform[2].y * vector.z(),
        transform[0].z * vector.x() + transform[1].z * vector.y() + transform[2].z * vector.z(),
    };
}

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

/// \brief Maps a uniform square sample to a cosine-weighted hemisphere.
///
/// The local positive z axis is the hemisphere pole.
[[nodiscard]] KIRA_HOST_DEVICE inline Vec3f cosineSampleHemisphere(Vec2f const &sample) noexcept {
    auto const disk = uniformSampleDisk(sample);
    auto const z = std::sqrt(std::max(0.0F, 1.0F - disk.norm2()));
    return {disk.x(), disk.y(), z};
}

/// \brief Returns the solid-angle PDF of cosine-weighted hemisphere sampling.
[[nodiscard]] KIRA_HOST_DEVICE inline float cosineHemispherePdf(Vec3f const &direction) noexcept {
    constexpr auto inversePi = std::numbers::inv_pi_v<float>;
    return std::max(direction.z(), 0.0F) * inversePi;
}
} // namespace flux
