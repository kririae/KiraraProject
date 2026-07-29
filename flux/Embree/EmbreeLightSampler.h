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
    /// \throw kira::Anyhow If the light count exceeds backend limits.
    void build(std::span<Ref<Light const> const> lights);

    /// \brief Releases all light data.
    void clear() noexcept;

    /// \brief Returns a sampler that borrows the current host data.
    [[nodiscard]] LightSampler getSampler() const noexcept;

private:
    std::vector<LightRecord> records_;
    std::vector<PointLight::Impl> pointLights_;
};
} // namespace flux
