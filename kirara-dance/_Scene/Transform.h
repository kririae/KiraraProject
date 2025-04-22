#pragma once

#include "Core/Math.h"

namespace krd {
class Transform final {
public:
    Transform() = default;

    ///
    Transform(float3 const &translation, float4 const &rotation, float3 const &scaling)
        : translation(translation), rotation(rotation), scaling(scaling) {}

    /// Create an identity transformation.
    [[nodiscard]] static Transform identity() { return Transform{}; }

    ///
    void setTranslation(float3 const &translation) { this->translation = translation; }
    ///
    [[nodiscard]] auto const &getTranslation() const { return translation; }

    ///
    void setRotation(float4 const &rotation) { this->rotation = rotation; }
    ///
    [[nodiscard]] auto const &getRotation() const { return rotation; }

    ///
    void setScaling(float3 const &scaling) { this->scaling = scaling; }
    ///
    [[nodiscard]] auto const &getScaling() const { return scaling; }

    ///
    /// Returns the TRS transformation matrix.
    [[nodiscard]] float4x4 getMatrix() const;

private:
    // By default, TRS order is used.
    float3 translation{0.0f};
    float4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion
    float3 scaling{1.0f};
};
} // namespace krd
