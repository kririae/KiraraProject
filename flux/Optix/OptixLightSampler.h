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
    /// \brief Creates an empty sampler bound to \p stream.
    explicit OptixLightSampler(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), records_(stream), pointLights_(stream) {}

    /// \brief Rebuilds device light data in light table order.
    ///
    /// Host staging remains valid until the owning OptixContext completes its
    /// sync stream.
    /// \throw kira::Anyhow If the light count exceeds device limits or CUDA
    /// cannot enqueue an allocation or copy.
    void build(std::span<Ref<Light const> const> lights);

    /// \brief Returns a sampler that borrows the current device data.
    [[nodiscard]] LightSampler getSampler() const noexcept;

private:
    std::vector<LightRecord> recordStaging_;
    std::vector<PointLight::Impl> pointLightStaging_;
    DeviceBuffer<LightRecord> records_;
    DeviceBuffer<PointLight::Impl> pointLights_;
};
} // namespace flux
