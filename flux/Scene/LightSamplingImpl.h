#pragma once

#include <cmath>
#include <cstdint>

#include "flux/Scene/EnvMapLightImpl.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Interaction.h"

namespace flux {
namespace detail {
/// \brief Returns the world-space interaction that \p interaction describes on primitive
/// \p primitiveIndex.
///
/// A parameterization gradient is a covector, so it transforms like a normal.
template <typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE SurfaceInteraction makeSurfaceInteraction(
    Scene const &scene, std::uint32_t primitiveIndex, GeometryInteraction const &interaction
) noexcept {
    return {
        .position = scene.transformPointToWorld(primitiveIndex, interaction.position),
        .geometricNormal =
            scene.transformNormalToWorld(primitiveIndex, interaction.geometricNormal).normalize(),
        .shadingNormal =
            scene.transformNormalToWorld(primitiveIndex, interaction.shadingNormal).normalize(),
        .uv = interaction.uv,
        .uvGradU = scene.transformNormalToWorld(primitiveIndex, interaction.uvGradU),
        .uvGradV = scene.transformNormalToWorld(primitiveIndex, interaction.uvGradV),
        .primitiveIndex = primitiveIndex,
        .elementIndex = interaction.elementIndex,
    };
}

/// \brief Returns the factor by which primitive \p primIndex scales the area of the surface at a
/// point with the world-space unit normal \p worldNormal.
///
/// A density over geometry-space area divided by it is the density over world-space area.
template <typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE float
computeAreaScale(Scene const &scene, std::uint32_t primIndex, Vec3f const &worldNormal) noexcept {
    return scene.lightSampler.table.primVolumeScales[primIndex] /
           scene.transformNormalToObject(primIndex, worldNormal).norm();
}

/// \brief Samples primitive \p primIndex, which \p scene holds as a light.
///
/// The pdf of the sample is the true density over solid angle at the context position, and equals
/// the one that \c pdfDirectLight returns for a hit at the same point. The radiance of the sample
/// is the radiance that a hit at the sampled point computes toward the same direction. Both
/// reconstruct the point with \c Geometry::Impl::computeInteraction and evaluate the EDF at it
/// with \p edfDispatcher and zero texture derivatives. MIS weights the two strategies by their
/// densities, so it is unbiased only while both hold.
template <typename Evaluator, typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE DirectLightSample samplePrimLight(
    Scene const &scene, EDF::Dispatcher const &edfDispatcher, LightSamplingContext const &ctx,
    std::uint32_t primIndex, Vec2f const &u
) noexcept {
    auto const &prim = scene.table.getPrimitive(primIndex);
    auto const &geometry = scene.table.getGeometry(prim.getGeometryIndex());
    auto const geomSample = geometry.sample(u);
    if (geomSample.pdf <= 0.0F)
        return {};

    auto const isect = makeSurfaceInteraction(
        scene, primIndex,
        geometry.computeInteraction(
            {.coordinates = geomSample.coordinates, .elementIndex = geomSample.elementIndex}
        )
    );
    auto const &p = isect.position;
    auto const &n = isect.geometricNormal;
    auto const d = p - ctx.position;
    auto const dist2 = d.norm2();
    if (!(dist2 > 0.0F))
        return {};

    auto const dist = std::sqrt(dist2);
    auto const wi = d / dist;
    auto const cosLight = std::abs(n.dot(-wi));
    auto const areaScale = computeAreaScale(scene, primIndex, n);
    if (!(cosLight > 0.0F) || !(areaScale > 0.0F))
        return {};

    // Convert the geometry-space area density to world-space solid angle.
    return {
        .radiance =
            edfDispatcher.evaluate<Evaluator>(scene.table.getEDF(prim.getEDFIndex()), isect, -wi),
        .wi = wi,
        .position = p,
        .pdf = geomSample.pdf / areaScale * dist2 / cosLight,
        .type = LightType::Primitive,
    };
}
} // namespace detail

template <typename Evaluator, typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE DirectLightSample sampleDirectLight(
    Scene const &scene, EDF::Dispatcher const &edfDispatcher, LightSamplingContext const &ctx,
    float uSelect, Vec2f const &uLight, bool hasEnvMap
) noexcept {
    auto const selected = scene.lightSampler.sample(ctx, uSelect);
    if (selected.pmf <= 0.0F)
        return {};

    auto const light = selected.light;
    if (light.type == LightType::Point) {
        auto sample = scene.lightSampler.table.points[light.index].sampleDirect(ctx);
        sample.pdf *= selected.pmf;
        return sample;
    }
    if (light.type == LightType::Primitive) {
        auto sample =
            detail::samplePrimLight<Evaluator>(scene, edfDispatcher, ctx, light.index, uLight);
        sample.pdf *= selected.pmf;
        return sample;
    }
    if (light.type == LightType::EnvMap && hasEnvMap) {
        auto sample =
            scene.lightSampler.table.envMap->template sampleDirect<Evaluator>(ctx, uLight);
        sample.pdf *= selected.pmf;
        return sample;
    }

    return {};
}

template <typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE float pdfDirectLight(
    Scene const &scene, LightSamplingContext const &ctx, SurfaceInteraction const &isect
) noexcept {
    auto const primIndex = isect.primitiveIndex;
    auto const selectPmf =
        scene.lightSampler.pmf(ctx, {.type = LightType::Primitive, .index = primIndex});
    if (!(selectPmf > 0.0F))
        return 0.0F;

    auto const d = isect.position - ctx.position;
    auto const dist2 = d.norm2();
    if (!(dist2 > 0.0F))
        return 0.0F;

    auto const wi = d / std::sqrt(dist2);
    auto const cosLight = std::abs(isect.geometricNormal.dot(-wi));
    auto const areaScale = detail::computeAreaScale(scene, primIndex, isect.geometricNormal);
    if (!(cosLight > 0.0F) || !(areaScale > 0.0F))
        return 0.0F;

    // Convert the geometry-space area density to world-space solid angle.
    auto const &prim = scene.table.getPrimitive(primIndex);
    auto const condPdf = scene.table.getGeometry(prim.getGeometryIndex()).pdf(isect.elementIndex) /
                         areaScale * dist2 / cosLight;
    return selectPmf * condPdf;
}

} // namespace flux
