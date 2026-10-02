#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Sampling/LightPowerDistribution.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"

namespace flux {
class Context;

/// \brief Per-light data read while rendering.
struct LightTable {
    /// Point lights by point-light index.
    PointLight::Impl const *points{};
    /// World-area scale of each primitive, by primitive index.
    float const *primAreaScales{};
    EnvMapLight::Impl const *envMap{};
};

/// \brief Host light data derived from the lights of a Context.
struct LightTableData final : private Noncopyable {
    static constexpr std::uint32_t invalidSlot = LightPowerDistribution::invalidSlot;

    /// What each light is, by light identity.
    struct {
        /// Point lights in Context ID order.
        std::vector<PointLight::Impl> points;

        /// World-area scale of each primitive's transform by primitive index, or
        /// zero for a primitive that is not a light.
        std::vector<float> primAreaScales;
    } lights;

    /// How a light is selected. A slot is a position in the selection distribution.
    struct {
        /// Light at each slot. A primitive's handle index is its primitive index.
        std::vector<LightHandle> handles;

        /// Cumulative selection weight at each slot.
        std::vector<float> cdf;

        /// Slot of each primitive by primitive index, or \c invalidSlot. A hole has none.
        std::vector<std::uint32_t> primSlots;

        /// Slot of the environment map, or \c invalidSlot.
        std::uint32_t envMapSlot{invalidSlot};
    } slots;

public:
    /// \brief Rebuilds the light data from \p context.
    ///
    /// Every primitive for which \c Primitive::isLight is true is a light. At most \p maxSlots
    /// lights get a slot, and emitting primitives lose theirs first. \p maxSlots is bounded by
    /// \c LightPowerDistribution::maxLightCount. \p envMapPower is empty when no
    /// environment map is active.
    void build(
        Context const &context, std::optional<float> envMapPower,
        std::uint32_t maxSlots = LightPowerDistribution::maxLightCount
    );

    void clear() noexcept;
};

static_assert(std::is_standard_layout_v<LightTable>);
static_assert(std::is_trivially_copyable_v<LightTable>);
} // namespace flux
