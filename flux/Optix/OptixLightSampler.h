#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"

namespace flux {
/// \brief Builds the OptiX light table and selection distribution.
class OptixLightSampler final : private Noncopyable, private CudaStreamMixin {
public:
    explicit OptixLightSampler(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), records_(stream), pointLights_(stream),
          primitiveIndices_(stream), primitiveAreaScales_(stream), powerCDF_(stream) {}

    /// \brief Rebuilds device light data in light table order.
    ///
    /// An empty span clears the light table.
    /// Host staging remains valid until the owning OptixContext completes its
    /// sync stream.
    void build(
        std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> primitives,
        std::span<Primitive::Impl> primitiveImpls
    );

    /// \brief Returns the current light sampler.
    ///
    /// The result remains valid until the next \c build.
    [[nodiscard]] LightSampler getSampler() const noexcept;

private:
    std::vector<LightRecord> recordStaging_;
    std::vector<PointLight::Impl> pointLightStaging_;
    std::vector<std::uint32_t> primitiveIndexStaging_;
    std::vector<float> primitiveAreaScaleStaging_;
    std::vector<float> powerCDFStaging_;
    DeviceBuffer<LightRecord> records_;
    DeviceBuffer<PointLight::Impl> pointLights_;
    DeviceBuffer<std::uint32_t> primitiveIndices_;
    DeviceBuffer<float> primitiveAreaScales_;
    DeviceBuffer<float> powerCDF_;
};
} // namespace flux
