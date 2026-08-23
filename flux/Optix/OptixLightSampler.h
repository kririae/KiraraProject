#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Scene/LightTableData.h"

namespace flux {
/// \brief Owns the light table and power distribution used by OptiX.
class OptixLightSampler final : private Noncopyable, private CudaStreamMixin {
public:
    struct Impl;

    explicit OptixLightSampler(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), records_(stream), pointLights_(stream), primIndices_(stream),
          primAreaScales_(stream), powerCDF_(stream) {}

    /// \brief Rebuilds the sampler and assigns light indices in \p primImpls.
    void build(
        std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> prims,
        std::span<Primitive::Impl> primImpls
    );

    /// \brief Returns the sampler used for rendering.
    ///
    /// The result remains valid until the next \c build.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    LightTableData staging_;
    std::vector<float> powerCDFStaging_;
    DeviceBuffer<LightRecord> records_;
    DeviceBuffer<PointLight::Impl> pointLights_;
    DeviceBuffer<std::uint32_t> primIndices_;
    DeviceBuffer<float> primAreaScales_;
    DeviceBuffer<float> powerCDF_;
};

/// \brief OptiX light table and selection distribution used during rendering.
struct OptixLightSampler::Impl {
    LightTable table{};
    LightPowerDistribution power{};

public:
    [[nodiscard]] KIRA_DEVICE inline SampledLight
    sample(LightSamplingContext const &ctx, float u) const noexcept {
        (void)ctx;
        return power.sample(u);
    }

    [[nodiscard]] KIRA_DEVICE inline float
    pmf(LightSamplingContext const &ctx, std::uint32_t lightIndex) const noexcept {
        (void)ctx;
        return power.pmf(lightIndex);
    }
};

static_assert(std::is_standard_layout_v<OptixLightSampler::Impl>);
static_assert(std::is_trivially_copyable_v<OptixLightSampler::Impl>);
} // namespace flux
