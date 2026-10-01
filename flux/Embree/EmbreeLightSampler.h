#pragma once

#include <optional>

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

    /// \brief Rebuilds the sampler from the lights of \p context.
    void
    build(Context const &context, EmbreeImageTexturePool::Impl imageTextures, float sceneRadius);

    void clear() noexcept;

    /// \brief Returns the sampler used for rendering.
    ///
    /// The result remains valid until the next \c build or \c clear.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    [[nodiscard]] float buildEnvMap(
        EnvMapLight const &envMap, EmbreeImageTexturePool::Impl imageTextures, float sceneRadius
    );

    LightTableData tableData_;
    kira::SmallVector<float, 0> envMapCDF_;
    kira::SmallVector<float, 0> envMapRows_;
    std::optional<EnvMapLight::Impl> envMap_;
};

/// \brief Embree light table and selection distribution used during rendering.
struct EmbreeLightSampler::Impl {
    LightTable table{};

    /// Light at each slot.
    LightHandle const *handles{};
    /// Slot of each primitive by primitive index.
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
        return {.light = handles[slot], .pmf = pmfValue};
    }

    /// \brief Returns the probability that \c sample selects \p light, or zero for a
    ///        point light.
    [[nodiscard]] float pmf(LightSamplingContext const &ctx, LightHandle light) const noexcept {
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

static_assert(std::is_standard_layout_v<EmbreeLightSampler::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeLightSampler::Impl>);
} // namespace flux
