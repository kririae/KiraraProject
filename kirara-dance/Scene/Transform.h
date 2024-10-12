#pragma once

#include "Core/Math.h"

namespace krd {
class Transform final {
public:
    Transform() = default;

    /// Create an identity transformation.
    [[nodiscard]] static Transform identity() { return Transform{}; }

    ///
    void setScaling(float3 const &scaling) { this->scaling = scaling; }
    ///
    [[nodiscard]] auto const &getScaling() const { return scaling; }

    ///
    void setRotation(float4 const &rotation) { this->rotation = rotation; }
    ///
    [[nodiscard]] auto const &getRotation() const { return rotation; }

    ///
    void setTranslation(float3 const &translation) { this->translation = translation; }
    ///
    [[nodiscard]] auto const &getTranslation() const { return translation; }

    ///
    /// Returns the SRT transformation matrix.
    [[nodiscard]] float4x4 getMatrix() const;

private:
    // By default, TRS order is used.
    float3 scaling{1.0f};
    float4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion
    float3 translation{0.0f};
};
} // namespace krd
