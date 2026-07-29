#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Scene/Light.h"

namespace flux {
/// \brief Owns the light data used by the Embree light sampler.
class EmbreeLightSampler final : private Noncopyable {
public:
    /// \brief Rebuilds host light data in light table order.
    ///
    /// An empty span clears the light table.
    void build(std::span<Ref<Light const> const> lights);

    void clear() noexcept;

    /// \brief Returns a view valid until the next \c build or \c clear.
    [[nodiscard]] LightSampler getSampler() const noexcept;

private:
    std::vector<LightRecord> records_;
    std::vector<PointLight::Impl> pointLights_;
};
} // namespace flux
