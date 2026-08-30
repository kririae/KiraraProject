#pragma once

#include <cmath>

#include "flux/Scene/EnvMapLightImpl.h"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Shading/EDF.h"

namespace flux {
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
        auto result = scene.lightSampler.table.pointLights[light.index].sampleDirect(ctx);
        result.pdf *= selected.pmf;
        return result;
    }

    if (light.type == LightType::EnvMap) {
        if (!hasEnvMap)
            return {};
        auto result =
            scene.lightSampler.table.envMap->template sampleDirect<Evaluator>(ctx, uLight);
        result.pdf *= selected.pmf;
        return result;
    }

    auto const primIndex = scene.lightSampler.table.primIndices[light.index];
    auto const &prim = scene.getPrimitive(primIndex);
    auto const areaScale = scene.lightSampler.table.primAreaScales[light.index];
    if (!(areaScale > 0.0F))
        return {};

    auto const geomSample = scene.getGeometry(prim.getGeometryIndex()).sample(uLight);
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
        .radiance = scene.getEDF(prim.getEDFIndex())
                        .evaluate({
                            .geometricNormal = n,
                            .wo = -wi,
                        }),
        .wi = wi,
        .distance = dist,
        .pdf = selected.pmf * geomSample.pdf / areaScale * dist2 / cosLight,
    };
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
    auto const condPdf = scene.getGeometry(prim.getGeometryIndex()).pdf(isect.elementIndex) /
                         areaScale * dist2 / cosLight;
    return scene.lightSampler.pmf(ctx, {.type = LightType::Primitive, .index = lightIndex}) *
           condPdf;
}

template <typename Evaluator, typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE Spectrum
evalEnvMap(Scene const &scene, Vec3f const &w) noexcept {
    auto const *envMap = scene.lightSampler.table.envMap;
    return envMap ? envMap->template eval<Evaluator>(w) : Spectrum{};
}

template <typename Scene>
[[nodiscard]] KIRA_HOST_DEVICE KIRA_FORCEINLINE float
pdfEnvMap(Scene const &scene, LightSamplingContext const &ctx, Vec3f const &w) noexcept {
    auto const *envMap = scene.lightSampler.table.envMap;
    if (!envMap)
        return 0.0F;
    return scene.lightSampler.pmf(ctx, {.type = LightType::EnvMap, .index = 0}) * envMap->pdf(w);
}
} // namespace flux
