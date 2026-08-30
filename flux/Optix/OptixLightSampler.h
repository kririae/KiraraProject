#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixImageTexturePool.h"
#include "flux/Scene/LightTableData.h"

namespace flux {
class Context;

/// \brief Owns the light table and power distribution used by OptiX.
class OptixLightSampler final : private Noncopyable, private CudaStreamMixin {
public:
    struct Impl;

    explicit OptixLightSampler(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Rebuilds the sampler and assigns primitive-light indices.
    void build(
        Context const &context, std::span<Primitive::Impl> primImpls,
        OptixImageTexturePool::Impl imageTextures, float sceneRadius
    );

    /// \brief Returns the sampler used for rendering.
    ///
    /// The result remains valid until the next \c build.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    [[nodiscard]] float buildEnvMap(
        EnvMapLight const &envMap, OptixImageTexturePool::Impl imageTextures, float sceneRadius
    );

    LightTableData staging_;
    std::vector<float> powerCDFStaging_;
    DeviceBuffer<LightHandle> lights_{getStream()};
    DeviceBuffer<PointLight::Impl> pointLights_{getStream()};
    DeviceBuffer<std::uint32_t> pointSlots_{getStream()};
    DeviceBuffer<std::uint32_t> primIndices_{getStream()};
    DeviceBuffer<float> primAreaScales_{getStream()};
    DeviceBuffer<std::uint32_t> primSlots_{getStream()};
    DeviceBuffer<float> envMapCDF_{getStream()};
    DeviceBuffer<float> envMapRows_{getStream()};
    DeviceBuffer<EnvMapLight::Impl> envMap_{getStream()};
    DeviceBuffer<float> powerCDF_{getStream()};
};

/// \brief OptiX light table and selection distribution used during rendering.
struct OptixLightSampler::Impl {
    LightTable table{};
    LightHandle const *lights{};
    std::uint32_t const *pointSlots{};
    std::uint32_t const *primSlots{};
    std::uint32_t envMapSlot{LightTableData::invalidSlot};
    LightPowerDistribution power{};

public:
    [[nodiscard]] KIRA_DEVICE inline SampledLight
    sample(LightSamplingContext const &ctx, float u) const noexcept {
        (void)ctx;
        float pmfValue;
        auto const slot = power.sample(u, pmfValue);
        if (pmfValue <= 0.0F)
            return {};
        return {.light = lights[slot], .pmf = pmfValue};
    }

    [[nodiscard]] KIRA_DEVICE inline float
    pmf(LightSamplingContext const &ctx, LightHandle light) const noexcept {
        (void)ctx;
        auto slot = LightTableData::invalidSlot;
        if (light.type == LightType::Point)
            slot = pointSlots[light.index];
        else if (light.type == LightType::Primitive)
            slot = primSlots[light.index];
        else if (light.type == LightType::EnvMap)
            slot = envMapSlot;
        return power.pmf(slot);
    }
};

static_assert(std::is_standard_layout_v<OptixLightSampler::Impl>);
static_assert(std::is_trivially_copyable_v<OptixLightSampler::Impl>);
} // namespace flux
