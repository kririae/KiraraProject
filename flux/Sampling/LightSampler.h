#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

#include "flux/Core/MathUtils.h"
#include "flux/Scene/Light.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief One light selected from a backend light table.
///
/// A zero PMF marks an empty selection. Light indices are dense only within
/// the backend sync that produced the sampler.
struct SampledLight {
    /// Sentinel used when no light can be selected.
    static constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();
    /// Dense index in LightSampler::lights.
    std::uint32_t lightIndex{invalidIndex};
    /// Discrete probability of selecting \c lightIndex.
    float pmf{};
};

/// \brief Light-selection view used by shared transport code.
///
/// Renderer backends own the light table and any persistent sampling data.
/// This view remains valid until the owning backend rebuilds or destroys its
/// light table.
struct LightSampler {
    /// Largest light table addressed without exceeding sampler precision.
    static constexpr std::uint32_t maxLightCount = 1U << 24U;

    /// Light table borrowed from the owning renderer backend.
    LightTable lights;

public:
    /// \brief Selects one light for \p context by estimated power.
    ///
    /// An empty light table returns an invalid selection.
    /// \pre \p sample is in \f$[0,1)\f$.
    /// \pre \c lights.numLights is at most \c maxLightCount.
    [[nodiscard]] KIRA_HOST_DEVICE SampledLight
    sample(LightSamplingContext const &context, float sample) const noexcept {
        (void)context;
        if (lights.numLights == 0)
            return {};
        if (lights.numLights == 1)
            return {.lightIndex = 0, .pmf = 1.0F};

        auto target = sample * lights.powerSum;
        if (!(target < lights.powerSum))
            target = std::nextafter(lights.powerSum, 0.0F);
        auto const index =
            static_cast<std::uint32_t>(upperBoundIndex(lights.powerCDF, lights.numLights, target));
        return {
            .lightIndex = index,
            .pmf = pmf(context, index),
        };
    }

    /// \brief Returns the selection probability of \p lightIndex.
    ///
    /// Returns zero when \p lightIndex is outside the light table.
    [[nodiscard]] KIRA_HOST_DEVICE float
    pmf(LightSamplingContext const &context, std::uint32_t lightIndex) const noexcept {
        (void)context;
        if (lightIndex >= lights.numLights)
            return 0.0F;
        if (lights.numLights == 1)
            return 1.0F;
        auto const previous = lightIndex == 0 ? 0.0F : lights.powerCDF[lightIndex - 1];
        return (lights.powerCDF[lightIndex] - previous) / lights.powerSum;
    }

    /// \brief Samples incident radiance from one selected light.
    ///
    /// \pre \p lightIndex is less than `lights.numLights`.
    template <typename BackendContext>
    [[nodiscard]] KIRA_HOST_DEVICE DirectLightSample sampleDirect(
        BackendContext const &backend, std::uint32_t lightIndex,
        LightSamplingContext const &context, Vec2f const &sample
    ) const noexcept {
        auto const record = lights.records[lightIndex];
        if (record.type == LightRecordType::Point)
            return lights.pointLights[record.typedIndex].sampleDirect(context);

        auto const primitiveIndex = lights.primitiveIndices[record.typedIndex];
        auto const &primitive = backend.getPrimitive(primitiveIndex);
        auto const areaScale = lights.primitiveAreaScales[record.typedIndex];
        if (!(areaScale > 0.0F))
            return {};
        auto const geometrySample =
            backend.getGeometry(primitive.getGeometryIndex()).sample(sample);
        if (geometrySample.pdf <= 0.0F)
            return {};

        auto const position =
            backend.transformPointToWorld(primitiveIndex, geometrySample.position);
        auto const geometricNormal =
            backend.transformNormalToWorld(primitiveIndex, geometrySample.geometricNormal)
                .normalize();
        auto const d = position - context.position;
        auto const dist2 = d.norm2();
        if (!(dist2 > 0.0F))
            return {};
        auto const distance = std::sqrt(dist2);
        auto const wi = d / distance;
        auto const cosLight = std::abs(geometricNormal.dot(-wi));
        if (!(cosLight > 0.0F))
            return {};

        return {
            .radiance = backend.getEDF(primitive.getEDFIndex())
                            .evaluate({
                                .geometricNormal = geometricNormal,
                                .wo = -wi,
                            }),
            .position = position,
            .wi = wi,
            .pdf = geometrySample.pdf / areaScale * dist2 / cosLight,
        };
    }

    template <typename BackendContext>
    [[nodiscard]] KIRA_HOST_DEVICE float pdfDirect(
        BackendContext const &backend, std::uint32_t lightIndex,
        LightSamplingContext const &context, SurfaceInteraction const &surface
    ) const noexcept {
        auto const record = lights.records[lightIndex];
        if (record.type != LightRecordType::Primitive)
            return 0.0F;

        auto const primitiveIndex = lights.primitiveIndices[record.typedIndex];
        auto const &primitive = backend.getPrimitive(primitiveIndex);
        auto const areaScale = lights.primitiveAreaScales[record.typedIndex];
        if (!(areaScale > 0.0F))
            return 0.0F;
        auto const d = surface.position - context.position;
        auto const dist2 = d.norm2();
        if (!(dist2 > 0.0F))
            return 0.0F;
        auto const wi = d / std::sqrt(dist2);
        auto const cosLight = std::abs(surface.geometricNormal.dot(-wi));
        if (!(cosLight > 0.0F))
            return 0.0F;
        return backend.getGeometry(primitive.getGeometryIndex()).pdf(surface.elementIndex) /
               areaScale * dist2 / cosLight;
    }
};

/// \brief Builds a normalized power CDF.
///
/// Each positive weight receives a probability representable by a 24-bit
/// uniform sample.
[[nodiscard]] std::vector<float> buildLightPowerCDF(std::span<float const> weights);

static_assert(std::is_standard_layout_v<SampledLight>);
static_assert(std::is_trivially_copyable_v<SampledLight>);
static_assert(std::is_standard_layout_v<LightSampler>);
static_assert(std::is_trivially_copyable_v<LightSampler>);
} // namespace flux
