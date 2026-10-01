#pragma once

#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixImageTexturePool.h"
#include "flux/Scene/LightTableData.h"

namespace flux {
class Context;
struct SceneTableData;

/// \brief Owns the light table and power distribution used by OptiX.
class OptixLightSampler final : private Noncopyable, private CudaStreamMixin {
public:
    struct Impl;

    explicit OptixLightSampler(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Rebuilds the sampler from \p scene and the lights of \p context.
    void build(
        SceneTableData const &scene, Context const &context,
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

    // Per-light data.
    DeviceBuffer<PointLight::Impl> points_{getStream()};
    DeviceBuffer<float> primAreaScales_{getStream()};
    DeviceBuffer<EnvMapLight::Impl> envMap_{getStream()};
    DeviceBuffer<float> envMapCDF_{getStream()};
    DeviceBuffer<float> envMapRows_{getStream()};

    // The selection distribution.
    DeviceBuffer<LightHandle> handles_{getStream()};
    DeviceBuffer<float> cdf_{getStream()};
    DeviceBuffer<std::uint32_t> primSlots_{getStream()};
};

/// \brief OptiX light table and selection distribution used during rendering.
struct OptixLightSampler::Impl {
    LightTable table{};

    /// Light at each slot.
    LightHandle const *handles{};
    /// Slot of each primitive by primitive index.
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
        return {.light = handles[slot], .pmf = pmfValue};
    }

    /// \brief Returns the probability that \c sample selects \p light, or zero for a
    ///        point light.
    [[nodiscard]] KIRA_DEVICE inline float
    pmf(LightSamplingContext const &ctx, LightHandle light) const noexcept {
        (void)ctx;
        // No ray hits a point light, so nothing asks for its probability.
        auto slot = LightTableData::invalidSlot;
        if (light.type == LightType::Primitive)
            slot = primSlots[light.index];
        else if (light.type == LightType::EnvMap)
            slot = envMapSlot;
        return power.pmf(slot);
    }
};

static_assert(std::is_standard_layout_v<OptixLightSampler::Impl>);
static_assert(std::is_trivially_copyable_v<OptixLightSampler::Impl>);
} // namespace flux
