#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Sampling/LightPowerDistribution.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"

namespace flux {
/// \brief Owns the host light data packed by backend samplers.
///
/// \c records and \c powers use the same dense light index. The next \c build
/// or \c clear invalidates the returned table.
struct LightTableData final : private Noncopyable {
    std::vector<LightRecord> records;
    std::vector<PointLight::Impl> pointLights;
    std::vector<std::uint32_t> primIndices;
    std::vector<float> primAreaScales;
    std::vector<float> powers;

public:
    /// \brief Rebuilds the light data and assigns light indices in \p primImpls.
    void build(
        std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> prims,
        std::span<Primitive::Impl> primImpls
    );

    void clear() noexcept;

    [[nodiscard]] LightTable getTable() const noexcept;
};
} // namespace flux
