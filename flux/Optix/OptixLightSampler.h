#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Scene/Light.h"

namespace flux {
/// \brief Owns the light data used by the OptiX light sampler.
class OptixLightSampler final : private Noncopyable, private CudaStreamMixin {
public:
    explicit OptixLightSampler(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), records_(stream), pointLights_(stream) {}

    /// \brief Rebuilds device light data in light table order.
    ///
    /// An empty span clears the light table.
    /// Host staging remains valid until the owning OptixContext completes its
    /// sync stream.
    void build(std::span<Ref<Light const> const> lights);

    /// \brief Returns a device view valid until the next \c build.
    [[nodiscard]] LightSampler getSampler() const noexcept;

private:
    std::vector<LightRecord> recordStaging_;
    std::vector<PointLight::Impl> pointLightStaging_;
    DeviceBuffer<LightRecord> records_;
    DeviceBuffer<PointLight::Impl> pointLights_;
};
} // namespace flux
