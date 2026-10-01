#pragma once

#include <cmath>
#include <cstdint>

#include "flux/Scene/EnvMapLightImpl.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/EDF.h"

namespace flux {
namespace detail {
template <typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE DirectLightSample samplePrimLight(
    Scene const &scene, LightSamplingContext const &ctx, std::uint32_t index, Vec2f const &u
) noexcept {
    auto const primIndex = scene.lightSampler.table.primIndices[index];
    auto const &prim = scene.table.getPrimitive(primIndex);
    auto const areaScale = scene.lightSampler.table.primAreaScales[index];
    if (!(areaScale > 0.0F))
        return {};

    auto const geomSample = scene.table.getGeometry(prim.getGeometryIndex()).sample(u);
    if (geomSample.pdf <= 0.0F)
        return {};

    auto const p = scene.transformPointToWorld(primIndex, geomSample.position);
    auto const n = scene.transformNormalToWorld(primIndex, geomSample.geometricNormal).normalize();
    auto const d = p - ctx.position;
    auto const dist2 = d.norm2();
    if (!(dist2 > 0.0F))
        return {};

    auto const dist = std::sqrt(dist2);
    auto const wi = d / dist;
    auto const cosLight = std::abs(n.dot(-wi));
    if (!(cosLight > 0.0F))
        return {};

    // Convert the geometry-space area density to world-space solid angle.
    return {
        .radiance = scene.table.getEDF(prim.getEDFIndex())
                        .evaluate({
                            .geometricNormal = n,
                            .wo = -wi,
                        }),
        .wi = wi,
        .position = p,
        .pdf = geomSample.pdf / areaScale * dist2 / cosLight,
        .type = LightType::Primitive,
    };
}
} // namespace detail

template <typename Evaluator, typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE DirectLightSample sampleDirectLight(
    Scene const &scene, LightSamplingContext const &ctx, float uSelect, Vec2f const &uLight,
    bool hasEnvMap
) noexcept {
    auto const selected = scene.lightSampler.sample(ctx, uSelect);
    if (selected.pmf <= 0.0F)
        return {};

    auto const light = selected.light;
    if (light.type == LightType::Point) {
        auto sample = scene.lightSampler.table.pointLights[light.index].sampleDirect(ctx);
        sample.pdf *= selected.pmf;
        return sample;
    }
    if (light.type == LightType::Primitive) {
        auto sample = detail::samplePrimLight(scene, ctx, light.index, uLight);
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
    Scene const &scene, LightSamplingContext const &ctx, Primitive::Impl const &prim,
    SurfaceInteraction const &isect
) noexcept {
    if (!prim.isLight())
        return 0.0F;

    auto const lightIndex = prim.getPrimLightIndex();
    auto const areaScale = scene.lightSampler.table.primAreaScales[lightIndex];
    if (!(areaScale > 0.0F))
        return 0.0F;

    auto const d = isect.position - ctx.position;
    auto const dist2 = d.norm2();
    if (!(dist2 > 0.0F))
        return 0.0F;

    auto const wi = d / std::sqrt(dist2);
    auto const cosLight = std::abs(isect.geometricNormal.dot(-wi));
    if (!(cosLight > 0.0F))
        return 0.0F;

    // Convert the geometry-space area density to world-space solid angle.
    auto const condPdf = scene.table.getGeometry(prim.getGeometryIndex()).pdf(isect.elementIndex) /
                         areaScale * dist2 / cosLight;
    return scene.lightSampler.pmf(ctx, {.type = LightType::Primitive, .index = lightIndex}) *
           condPdf;
}

} // namespace flux
