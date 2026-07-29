#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete light implementation.
enum class LightType : std::uint8_t {
    Point,
    Count,
};

/// \brief Result of sampling incident radiance from one selected light.
///
/// \c wi points from the surface toward the light. \c pdf is conditional on
/// the light already being selected. A zero PDF marks an invalid sample.
struct DirectLightSample {
    /// Incident radiance along \c wi.
    Spectrum radiance{};

    /// World-space direction from the surface toward the light.
    Vec3f wi{};

    /// Distance from the surface to the sampled light point.
    float distance{};

    /// Conditional sampling density, or unit mass for a delta light.
    float pdf{};

    /// Whether the sampled light has a discrete directional distribution.
    bool delta{};
};

/// \brief Host-side base for authored lights.
class Light : public RenderObject {
protected:
    /// \brief Creates a light of \p type in \p tx.
    Light(TXContext &tx, kira::Properties properties, LightType type);

public:
    /// \brief Returns the concrete light type.
    [[nodiscard]] LightType getType() const noexcept { return type_; }

private:
    LightType type_;
};

/// \brief Isotropic point light with RGB radiant intensity.
///
/// \par Properties
/// - \c position: optional world-space Vec3f position; defaults to zero.
/// - \c intensity: optional nonnegative RGB radiant intensity; defaults to one.
class PointLight final : public Light {
    friend class TXContext;

public:
    /// \brief Compact point-light implementation shared by all backends.
    struct Impl;

    /// \brief Returns the world-space light position.
    [[nodiscard]] Vec3f const &getPosition() const noexcept { return position_; }

    /// \brief Sets the world-space light position.
    ///
    /// \throw kira::Anyhow If \p position is not finite.
    void setPosition(Vec3f const &position);

    /// \brief Returns the RGB radiant intensity.
    [[nodiscard]] Spectrum const &getIntensity() const noexcept { return intensity_; }

    /// \brief Sets the RGB radiant intensity.
    ///
    /// \throw kira::Anyhow If a component is negative or non-finite.
    void setIntensity(Spectrum const &intensity);

    /// \brief Builds the implementation used by renderer backends.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    PointLight(TXContext &tx, kira::Properties properties);

    Vec3f position_{};
    Spectrum intensity_{1.0F, 1.0F, 1.0F};
};

/// \brief Compact point-light implementation shared by all backends.
struct PointLight::Impl {
    /// World-space light position.
    Vec3f position{};

    /// RGB radiant intensity.
    Spectrum intensity{};

public:
    /// \brief Samples incident radiance at \p surface.
    ///
    /// A point light has one discrete direction, so a valid sample has unit
    /// conditional mass and \c delta set.
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample
    sampleDirect(SurfaceInteraction const &surface) const noexcept;
};

KIRA_HOST_DEVICE inline DirectLightSample
PointLight::Impl::sampleDirect(SurfaceInteraction const &surface) const noexcept {
    auto const offset = position - surface.position;
    auto const squaredDistance = offset.norm2();
    if (!(squaredDistance > 0.0F))
        return {};

    auto const distance = std::sqrt(squaredDistance);
    return {
        .radiance = intensity / squaredDistance,
        .wi = offset / distance,
        .distance = distance,
        .pdf = 1.0F,
        .delta = true,
    };
}

/// \brief Maps a light table index to one concrete dense array.
struct LightRecord {
    /// Concrete array selected by this record.
    LightType type{};

    /// Index within the selected concrete array.
    std::uint32_t typedIndex{};
};

/// \brief Non-owning view of the lights built by one renderer backend.
struct LightTable {
    /// Light records in selection order.
    LightRecord const *records{};

    /// Dense point-light implementations.
    PointLight::Impl const *pointLights{};

    /// Number of light records.
    std::uint32_t numLights{};

public:
    /// \brief Samples the selected light.
    ///
    /// \param lightIndex Dense index returned by LightSampler.
    /// \param surface World-space shading point.
    /// \param sample Uniform sample used by non-delta light types.
    /// \pre \p lightIndex is less than \c numLights.
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample sampleDirect(
        std::uint32_t lightIndex, SurfaceInteraction const &surface, Vec2f const &sample
    ) const noexcept {
        (void)sample;
        auto const record = records[lightIndex];
        switch (record.type) {
        case LightType::Point: return pointLights[record.typedIndex].sampleDirect(surface);
        case LightType::Count: break;
        }
        return {};
    }
};

static_assert(std::is_standard_layout_v<DirectLightSample>);
static_assert(std::is_trivially_copyable_v<DirectLightSample>);
static_assert(std::is_standard_layout_v<PointLight::Impl>);
static_assert(std::is_trivially_copyable_v<PointLight::Impl>);
static_assert(std::is_standard_layout_v<LightRecord>);
static_assert(std::is_trivially_copyable_v<LightRecord>);
static_assert(std::is_standard_layout_v<LightTable>);
static_assert(std::is_trivially_copyable_v<LightTable>);
} // namespace flux
