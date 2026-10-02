#pragma once

#include <cmath>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Orthonormal shading frame whose local z axis is the normal.
class Frame {
public:
    /// \brief Builds a stable basis around \p normal.
    ///
    /// \pre \p normal is normalized.
    KIRA_HOST_DEVICE explicit Frame(Vec3f const &normal) noexcept : normal_(normal) {
        auto const sign = std::copysign(1.0F, normal.z());
        auto const a = -1.0F / (sign + normal.z());
        auto const b = normal.x() * normal.y() * a;
        tangent_[0] = 1.0F + sign * normal.x() * normal.x() * a;
        tangent_[1] = sign * b;
        tangent_[2] = -sign * normal.x();
        bitangent_[0] = b;
        bitangent_[1] = sign + normal.y() * normal.y() * a;
        bitangent_[2] = -normal.y();
    }

    /// \brief Uses the supplied orthonormal basis.
    ///
    /// \pre The three vectors are normalized and mutually orthogonal.
    KIRA_HOST_DEVICE
    Frame(Vec3f const &tangent, Vec3f const &bitangent, Vec3f const &normal) noexcept
        : tangent_(tangent), bitangent_(bitangent), normal_(normal) {}

    /// \brief Converts a world-space vector to this local frame.
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f toLocal(Vec3f const &value) const noexcept {
        return {
            value.dot(tangent_),
            value.dot(bitangent_),
            value.dot(normal_),
        };
    }

    /// \brief Converts a local vector to world space.
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f toWorld(Vec3f const &value) const noexcept {
        return tangent_ * value.x() + bitangent_ * value.y() + normal_ * value.z();
    }

    /// \brief Returns the cosine to the local normal.
    [[nodiscard]] KIRA_HOST_DEVICE static float cosTheta(Vec3f const &value) noexcept {
        return value.z();
    }

    /// \brief Returns the absolute cosine to the local normal.
    [[nodiscard]] KIRA_HOST_DEVICE static float absCosTheta(Vec3f const &value) noexcept {
        return std::abs(value.z());
    }

    /// \brief Returns whether both directions lie in the same open hemisphere.
    [[nodiscard]] KIRA_HOST_DEVICE static bool
    sameHemisphere(Vec3f const &lhs, Vec3f const &rhs) noexcept {
        return lhs.z() * rhs.z() > 0.0F;
    }

private:
    Vec3f tangent_;
    Vec3f bitangent_;
    Vec3f normal_;
};
} // namespace flux
