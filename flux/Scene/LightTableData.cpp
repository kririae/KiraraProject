#include "flux/Scene/LightTableData.h"

#include <cstddef>

#include "flux/Core/Logging.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
void LightTableData::buildFinite(Context const &context, std::span<Primitive::Impl> primImpls) {
    clear();
    auto const lights = context.getObjects<Light>();
    auto const prims = context.getObjects<Primitive>();
    handles.reserve(lights.size() + prims.size());
    pointLights.reserve(lights.size());
    pointSlots.reserve(lights.size());
    primIndices.reserve(prims.size());
    primAreaScales.reserve(prims.size());
    primSlots.reserve(prims.size());
    powers.reserve(lights.size() + prims.size());

    for (auto const &light : lights) {
        if (handles.size() >= LightPowerDistribution::maxLightCount)
            break;

        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "LightTableData: light type does not match its host implementation"
                );

            auto const pointIndex = static_cast<std::uint32_t>(pointLights.size());
            pointSlots.push_back(static_cast<std::uint32_t>(handles.size()));
            handles.push_back({.type = LightType::Point, .index = pointIndex});
            pointLights.push_back(point->getImpl());
            powers.push_back(point->estimatePower());
            break;
        }
        case LightType::EnvMap: break;
        case LightType::Primitive:
        case LightType::Count: throw kira::Anyhow("LightTableData: unsupported light type");
        }
    }

    std::size_t primIndex = 0;
    for (auto const &prim : prims) {
        if (!prim->isVisible())
            continue;
        if (primIndex >= primImpls.size())
            throw kira::Anyhow("LightTableData: primitive tables do not match");
        auto const densePrimIndex = primIndex++;
        auto &primImpl = primImpls[densePrimIndex];
        primImpl.primLightIndex = Primitive::Impl::invalidPrimLightIndex;
        if (!prim->getEDF())
            continue;
        if (handles.size() >= LightPowerDistribution::maxLightCount)
            continue;
        if (prim->hasNonUniformScale())
            LogWarn(
                "LightTableData: emissive primitive {} has non-uniform scale; using its average "
                "scale for light sampling",
                prim->getContextId()
            );

        auto const lightIndex = static_cast<std::uint32_t>(primIndices.size());
        primImpl.primLightIndex = lightIndex;
        primSlots.push_back(static_cast<std::uint32_t>(handles.size()));
        handles.push_back({.type = LightType::Primitive, .index = lightIndex});
        primIndices.push_back(static_cast<std::uint32_t>(densePrimIndex));
        primAreaScales.push_back(prim->estimateAreaScale());
        powers.push_back(prim->estimatePower());
    }
    if (primIndex != primImpls.size())
        throw kira::Anyhow("LightTableData: primitive tables do not match");
}

void LightTableData::addEnvMap(float power) {
    if (handles.size() >= LightPowerDistribution::maxLightCount)
        return;
    envMapSlot = static_cast<std::uint32_t>(handles.size());
    handles.push_back({.type = LightType::EnvMap, .index = 0});
    powers.push_back(power);
}

void LightTableData::clear() noexcept {
    handles.clear();
    pointLights.clear();
    pointSlots.clear();
    primIndices.clear();
    primAreaScales.clear();
    primSlots.clear();
    powers.clear();
    envMapSlot = invalidSlot;
}

LightTable LightTableData::getTable(EnvMapLight::Impl const *envMap) const noexcept {
    return {
        .pointLights = pointLights.data(),
        .primIndices = primIndices.data(),
        .primAreaScales = primAreaScales.data(),
        .envMap = envMap,
    };
}

} // namespace flux
