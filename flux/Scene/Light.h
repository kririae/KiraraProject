#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete light implementation.
enum class LightType : std::uint8_t {
    Point,
    Count,
};

/// \brief Path vertex data used to select and sample lights.
struct LightSamplingContext {
    /// World-space receiving position.
    Vec3f position{};
    /// World-space shading normal at a surface vertex.
    Vec3f normal{};
};

/// \brief Result of sampling incident radiance from one selected light.
///
/// \c wi points from the surface toward the light. For a non-delta light,
/// \c pdf is the conditional solid-angle density after light selection. A
/// delta light uses unit mass. A zero PDF marks an invalid sample.
struct DirectLightSample {
    /// Incident radiance along \c wi.
    Spectrum radiance{};
    /// World-space sampled position.
    Vec3f position{};
    /// World-space direction from the surface toward the light.
    Vec3f wi{};
    /// Distance from the surface to the sampled light point.
    float distance{};
    /// Conditional solid-angle density, or unit mass for a delta light.
    float pdf{};
    /// Whether the sampled light has a discrete directional distribution.
    bool delta{};
};

/// \brief Host-side base for authored lights.
class Light : public RenderObject {
protected:
    Light(TXContext &tx, LightType type);

public:
    [[nodiscard]] LightType getType() const noexcept { return type_; }
    [[nodiscard]] virtual float estimatePower() const noexcept = 0;

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
    struct Impl;

    /// World-space light position.
    [[nodiscard]] Vec3f const &getPosition() const noexcept { return position_; }

    /// \brief Sets the world-space light position.
    ///
    /// All coordinates must be finite.
    void setPosition(Vec3f const &position);

    /// RGB radiant intensity.
    [[nodiscard]] Spectrum const &getIntensity() const noexcept { return intensity_; }

    /// \brief Sets the RGB radiant intensity.
    ///
    /// All components must be finite and nonnegative.
    void setIntensity(Spectrum const &intensity);

    [[nodiscard]] Impl getImpl() const noexcept;
    [[nodiscard]] float estimatePower() const noexcept override;

private:
    PointLight(TXContext &tx, kira::Properties const &props);

    Vec3f position_{};
    Spectrum intensity_{1.0F, 1.0F, 1.0F};
};

struct PointLight::Impl {
    /// World-space light position.
    Vec3f position{};
    /// RGB radiant intensity.
    Spectrum intensity{};

public:
    /// \brief Samples incident radiance at \p context.
    ///
    /// A point light has one discrete direction, so a valid sample has unit
    /// conditional mass and \c delta set. A coincident receiving position
    /// produces an invalid sample.
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample
    sampleDirect(LightSamplingContext const &context) const noexcept;
};

KIRA_HOST_DEVICE inline DirectLightSample
PointLight::Impl::sampleDirect(LightSamplingContext const &context) const noexcept {
    auto const d = position - context.position;
    auto const dist2 = d.norm2();
    if (!(dist2 > 0.0F))
        return {};

    auto const dist = std::sqrt(dist2);
    return {
        .radiance = intensity / dist2,
        .position = position,
        .wi = d / dist,
        .distance = dist,
        .pdf = 1.0F,
        .delta = true,
    };
}

enum class LightRecordType : std::uint8_t {
    Point,
    Primitive,
};

/// \brief Maps one selectable light to backend data.
struct LightRecord {
    LightRecordType type{};
    /// Index within the selected concrete array.
    std::uint32_t typedIndex{};
};

/// \brief Non-owning view of selectable lights built by one renderer backend.
struct LightTable {
    /// Light records in selection order.
    LightRecord const *records{};
    /// Dense point-light implementations.
    PointLight::Impl const *pointLights{};
    /// Dense primitive indices for emissive primitive records.
    std::uint32_t const *primitiveIndices{};
    /// Estimated object-to-world area scales for emissive primitives.
    float const *primitiveAreaScales{};
    /// Cumulative estimated light powers in record order.
    float const *powerCDF{};
    /// Final value of \c powerCDF.
    float powerSum{};
    /// Number of records in \c records.
    std::uint32_t numLights{};
};

static_assert(std::is_standard_layout_v<DirectLightSample>);
static_assert(std::is_trivially_copyable_v<DirectLightSample>);
static_assert(std::is_standard_layout_v<LightSamplingContext>);
static_assert(std::is_trivially_copyable_v<LightSamplingContext>);
static_assert(std::is_standard_layout_v<PointLight::Impl>);
static_assert(std::is_trivially_copyable_v<PointLight::Impl>);
static_assert(std::is_standard_layout_v<LightRecord>);
static_assert(std::is_trivially_copyable_v<LightRecord>);
static_assert(std::is_standard_layout_v<LightTable>);
static_assert(std::is_trivially_copyable_v<LightTable>);
} // namespace flux
