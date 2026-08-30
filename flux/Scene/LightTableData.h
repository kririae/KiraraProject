#pragma once

#include <cstdint>
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
/// \c handles and \c powers use the same selection slot. The next \c build or
/// \c clear invalidates the returned table.
struct LightTableData final : private Noncopyable {
    static constexpr std::uint32_t invalidSlot = LightPowerDistribution::invalidSlot;

    std::vector<LightHandle> handles;
    std::vector<PointLight::Impl> pointLights;
    std::vector<std::uint32_t> pointSlots;
    std::vector<std::uint32_t> primIndices;
    std::vector<float> primAreaScales;
    std::vector<std::uint32_t> primSlots;
    std::vector<float> powers;
    std::uint32_t envMapSlot{invalidSlot};

public:
    /// \brief Rebuilds finite light data and assigns primitive-light indices.
    ///
    /// \p primImpls follows the visible primitive order in \p context.
    void buildFinite(Context const &context, std::span<Primitive::Impl> primImpls);

    /// \brief Adds the active environment map to the selection data.
    void addEnvMap(float power);

    void clear() noexcept;

    [[nodiscard]] LightTable getTable(EnvMapLight::Impl const *envMap) const noexcept;
};

static_assert(std::is_standard_layout_v<LightTable>);
static_assert(std::is_trivially_copyable_v<LightTable>);
} // namespace flux
