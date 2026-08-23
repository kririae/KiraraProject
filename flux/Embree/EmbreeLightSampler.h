#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"

namespace flux {
/// \brief Builds the Embree light table and selection distribution.
class EmbreeLightSampler final : private Noncopyable {
public:
    /// \brief Rebuilds host light data in light table order.
    ///
    /// An empty span clears the light table.
    void build(
        std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> primitives,
        std::span<Primitive::Impl> primitiveImpls
    );

    void clear() noexcept;

    /// \brief Returns the current light sampler.
    ///
    /// The result remains valid until the next \c build or \c clear.
    [[nodiscard]] LightSampler getSampler() const noexcept;

private:
    std::vector<LightRecord> records_;
    std::vector<PointLight::Impl> pointLights_;
    std::vector<std::uint32_t> primitiveIndices_;
    std::vector<float> primitiveAreaScales_;
    std::vector<float> powerCDF_;
};
} // namespace flux
