#pragma once

#include <array>
#include <type_traits>

#include "flux/Core/EnumFlags.h"
#include "flux/Sampling/Distribution2D.h"
#include "flux/Scene/Light.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Environment-map light.
///
/// \par Properties
/// - \c rotation: optional Euler angles in degrees, applied about X, then Y, then Z.
class EnvMapLight final : public Light {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Properties of an environment map that a setter can change.
    ///
    /// One bit per setter. A setter records its bit only when the value changes.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        Texture = 1U << 0U,
        Scale = 1U << 1U,
        Rotation = 1U << 2U,
    };

    [[nodiscard]] Ref<Texture const> const &getTexture() const noexcept { return texture_; }

    /// \brief Replaces the texture.
    ///
    /// \pre \p texture is not empty and belongs to the same Context.
    void setTexture(Ref<Texture const> texture);

    [[nodiscard]] Spectrum const &getScale() const noexcept { return scale_; }

    /// Sets the radiance scale.
    void setScale(Spectrum const &scale);

    /// \brief Returns the row-major rotation from the environment map's frame to world space.
    [[nodiscard]] std::array<float, 9> const &getRotation() const noexcept { return rotation_; }

    /// \brief Replaces the rotation from the environment map's frame to world space.
    ///
    /// \pre \p rotation is a rotation: orthonormal, with determinant +1.
    void setRotation(std::array<float, 9> const &rotation);

    /// \brief Builds the runtime light using backend-owned sampling data.
    [[nodiscard]] Impl getImpl(Distribution2D distribution) const;

    [[nodiscard]] float estimatePower(float sceneRadius, float luminanceIntegral) const noexcept;

private:
    EnvMapLight(TXContext &tx, kira::Properties const &props);

    Ref<Texture const> texture_;
    Spectrum scale_{1.0F, 1.0F, 1.0F};
    std::array<float, 9> rotation_{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
};

struct EnvMapLight::Impl {
    Texture::Impl texture;
    Distribution2D distribution;
    Spectrum scale;
    /// Row-major world-to-environment rotation.
    std::array<float, 9> worldToEnv;

public:
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum eval(Vec3f const &w, float angle = 0.0F) const noexcept;

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evalAndPdf(Vec3f const &w, float &pdf) const noexcept;

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum
    evalAndPdf(Vec3f const &w, float angle, float &pdf) const noexcept;

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample
    sampleDirect(LightSamplingContext const &ctx, Vec2f u) const noexcept;

    [[nodiscard]] KIRA_HOST_DEVICE float pdf(Vec3f const &w) const noexcept;

private:
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum
    evalUV(Vec2f uv, float angle, float sinTheta) const noexcept;

    [[nodiscard]] KIRA_HOST_DEVICE float pdfUV(Vec2f uv, float sinTheta) const noexcept;
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f toLocal(Vec3f const &w) const noexcept;
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f toWorld(Vec3f const &w) const noexcept;
    [[nodiscard]] KIRA_HOST_DEVICE static Vec2f directionToUV(Vec3f const &w) noexcept;
};

template <> inline constexpr bool isEnumFlags<EnvMapLight::DirtyBits> = true;

static_assert(std::is_standard_layout_v<EnvMapLight::Impl>);
static_assert(std::is_trivially_copyable_v<EnvMapLight::Impl>);
} // namespace flux
