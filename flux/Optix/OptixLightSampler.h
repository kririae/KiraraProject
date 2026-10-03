#pragma once

#include <cstddef>
#include <optional>

#include "flux/Core/Object.h"
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixImageTexture.h"
#include "flux/Scene/LightTableData.h"

namespace flux {
class Context;

/// \brief Owns the light table and power distribution used by OptiX.
class OptixLightSampler final : private Noncopyable, private CudaStreamMixin {
public:
    struct Impl;

    explicit OptixLightSampler(DeletionQueue &deletionQueue) noexcept
        : CudaStreamMixin(deletionQueue.getStream()), deletionQueue_(deletionQueue) {}

    /// \brief Brings the sampler up to date with the lights of \p context.
    ///
    /// \p imageTextures is read only during the call.
    /// \pre \p imageTextures holds the image textures of \p context at their indices.
    /// \throw kira::Anyhow If CUDA fails. Then the sampler is valid only for destruction.
    void sync(Context const &context, OptixImageTextureTable imageTextures, float sceneRadius);

    /// \brief Returns the sampler used for rendering.
    ///
    /// The result remains valid until the next \c sync.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    /// What the sampling distribution of an image environment map depends on. Equal keys give
    /// equal distributions.
    struct EnvMapKey {
        std::size_t imageId{};
        ImageComponentMapping componentMapping;
        Spectrum scale;

        friend bool operator==(EnvMapKey const &, EnvMapKey const &) = default;
    };

    /// The sampling distribution of an image environment map, and what it was built for. The CDFs
    /// are empty when the weight sum is zero.
    struct EnvMapDistribution {
        EnvMapKey key;
        Vec2u extent;
        float weightSum{};
        DeviceBuffer<float> cdf;
        DeviceBuffer<float> rows;
    };

    /// \brief Builds the sampling distribution of \p image for \p key.
    /// \pre \p key describes \p image.
    [[nodiscard]] EnvMapDistribution buildEnvMapDistribution(
        ImageTexture const &image, EnvMapKey const &key, OptixImageTextureTable imageTextures
    ) const;

    /// \brief Uploads the environment map light, and returns its power.
    /// \param distribution The distribution of the image of \p envMap, or null if it has a
    ///        constant texture.
    [[nodiscard]] float uploadEnvMap(
        EnvMapLight const &envMap, EnvMapDistribution const *distribution, float sceneRadius
    );

    DeletionQueue &deletionQueue_;

    // Host light tables of the last sync.
    LightTableData table_;

    // Per-light data.
    DeviceBuffer<PointLight::Impl> points_{getStream()};
    DeviceBuffer<float> primAreaScales_{getStream()};
    DeviceBuffer<EnvMapLight::Impl> envMap_{getStream()};

    // After each successful sync, set iff the active environment map has an image texture, and
    // then built for its key.
    std::optional<EnvMapDistribution> envMapDistribution_;

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
