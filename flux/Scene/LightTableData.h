#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Sampling/LightPowerDistribution.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"

namespace flux {
class Context;

/// \brief Light data used during rendering.
struct LightTable {
    PointLight::Impl const *pointLights{};
    std::uint32_t const *primIndices{};
    float const *primAreaScales{};
    EnvMapLight::Impl const *envMap{};
};

/// \brief Owns the host light data packed by backend samplers.
///
/// A slot indexes matching entries in \c handles, \c powers, and the sampler
/// CDF.
struct LightTableData final : private Noncopyable {
    static constexpr std::uint32_t invalidSlot = LightPowerDistribution::invalidSlot;

    std::vector<LightHandle> handles;
    std::vector<PointLight::Impl> pointLights;
    /// Slot for each point-light index.
    std::vector<std::uint32_t> pointSlots;
    /// Backend scene primitive index for each primitive-light index.
    std::vector<std::uint32_t> primIndices;
    /// World-area scale for each primitive-light index.
    std::vector<float> primAreaScales;
    /// Slot for each primitive-light index.
    std::vector<std::uint32_t> primSlots;
    std::vector<float> powers;
    /// Slot for the environment map, or \c invalidSlot when it is not selectable.
    std::uint32_t envMapSlot{invalidSlot};

public:
    /// \brief Rebuilds the light data and assigns primitive-light indices.
    ///
    /// \p primImpls follows the visible primitive order in \p context.
    /// \p envMapPower is empty when no environment map is active.
    void build(
        Context const &context, std::span<Primitive::Impl> primImpls,
        std::optional<float> envMapPower
    );

    void clear() noexcept;
};

static_assert(std::is_standard_layout_v<LightTable>);
static_assert(std::is_trivially_copyable_v<LightTable>);
} // namespace flux
