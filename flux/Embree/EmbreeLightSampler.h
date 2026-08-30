#pragma once

#include <optional>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Embree/EmbreeImageTexturePool.h"
#include "flux/Scene/LightTableData.h"
#include "kira/SmallVector.h"

namespace flux {
class Context;

/// \brief Owns the light table and power distribution used by Embree.
class EmbreeLightSampler final : private Noncopyable {
public:
    struct Impl;

    /// \brief Rebuilds the sampler and assigns primitive-light indices.
    void build(
        Context const &context, std::span<Primitive::Impl> primImpls,
        EmbreeImageTexturePool::Impl imageTextures, float sceneRadius
    );

    void clear() noexcept;

    /// \brief Returns the sampler used for rendering.
    ///
    /// The result remains valid until the next \c build or \c clear.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    LightTableData tableData_;
    std::vector<float> powerCDF_;
    kira::SmallVector<float, 0> envMapCDF_;
    kira::SmallVector<float, 0> envMapRows_;
    std::optional<EnvMapLight::Impl> envMap_;
};

/// \brief Embree light table and selection distribution used during rendering.
struct EmbreeLightSampler::Impl {
    LightTable table{};
    LightHandle const *lights{};
    std::uint32_t const *pointSlots{};
    std::uint32_t const *primSlots{};
    std::uint32_t envMapSlot{LightTableData::invalidSlot};
    LightPowerDistribution power{};

public:
    [[nodiscard]] SampledLight sample(LightSamplingContext const &ctx, float u) const noexcept {
        (void)ctx;
        float pmfValue;
        auto const slot = power.sample(u, pmfValue);
        if (pmfValue <= 0.0F)
            return {};
        return {.light = lights[slot], .pmf = pmfValue};
    }

    [[nodiscard]] float pmf(LightSamplingContext const &ctx, LightHandle light) const noexcept {
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

static_assert(std::is_standard_layout_v<EmbreeLightSampler::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeLightSampler::Impl>);
} // namespace flux
