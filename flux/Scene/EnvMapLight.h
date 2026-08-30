#pragma once

#include <array>
#include <type_traits>

#include "flux/Sampling/Distribution2D.h"
#include "flux/Scene/Light.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Environment-map light.
class EnvMapLight final : public Light {
    friend class TXContext;

public:
    struct Impl;

    [[nodiscard]] Ref<Texture const> const &getTexture() const noexcept { return texture_; }

    /// Replaces the texture with one from the same Context.
    void setTexture(Ref<Texture const> texture);

    [[nodiscard]] Spectrum const &getScale() const noexcept { return scale_; }

    /// Sets the radiance scale. Negative components are clamped to zero.
    void setScale(Spectrum const &scale);

    /// Euler rotation in degrees, applied around X, Y, then Z.
    [[nodiscard]] Vec3f const &getRotation() const noexcept { return rotation_; }

    /// Sets the Euler rotation in degrees.
    void setRotation(Vec3f const &rotation);

    /// \brief Builds the runtime light using backend-owned sampling data.
    [[nodiscard]] Impl getImpl(Distribution2D distribution) const;

    [[nodiscard]] float estimatePower(float sceneRadius, float luminanceIntegral) const noexcept;

private:
    EnvMapLight(TXContext &tx, kira::Properties const &props);
    void registerTo(TXContext &tx) override;

    Ref<Texture const> texture_;
    Spectrum scale_{1.0F, 1.0F, 1.0F};
    Vec3f rotation_{};
};

struct EnvMapLight::Impl {
    Texture::Impl texture;
    Distribution2D distribution;
    Spectrum scale;
    /// Row-major world-to-environment rotation.
    std::array<float, 9> worldToEnv;

public:
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum eval(Vec3f const &w) const noexcept;

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evalAndPdf(Vec3f const &w, float &pdf) const noexcept;

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample
    sampleDirect(LightSamplingContext const &ctx, Vec2f u) const noexcept;

    [[nodiscard]] KIRA_HOST_DEVICE float pdf(Vec3f const &w) const noexcept;

private:
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evalUV(Vec2f uv) const noexcept;

    [[nodiscard]] KIRA_HOST_DEVICE float pdfUV(Vec2f uv, float sinTheta2) const noexcept;
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f toLocal(Vec3f const &w) const noexcept;
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f toWorld(Vec3f const &w) const noexcept;
    [[nodiscard]] KIRA_HOST_DEVICE static Vec2f directionToUV(Vec3f const &w) noexcept;
};

static_assert(std::is_standard_layout_v<EnvMapLight::Impl>);
static_assert(std::is_trivially_copyable_v<EnvMapLight::Impl>);
} // namespace flux
