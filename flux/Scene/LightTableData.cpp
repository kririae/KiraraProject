#include "flux/Scene/LightTableData.h"

#include <cstddef>

#include "flux/Core/Logging.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
void LightTableData::build(
    std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> prims,
    std::span<Primitive::Impl> primImpls
) {
    if (lights.size() > LightPowerDistribution::maxLightCount)
        throw kira::Anyhow("LightTableData: light count exceeds sampler resolution");
    if (primImpls.size() != prims.size())
        throw kira::Anyhow("LightTableData: primitive tables do not match");

    clear();
    records.reserve(lights.size() + prims.size());
    pointLights.reserve(lights.size());
    primIndices.reserve(prims.size());
    primAreaScales.reserve(prims.size());
    powers.reserve(lights.size() + prims.size());

    for (auto const &light : lights) {
        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "LightTableData: light type does not match its host implementation"
                );

            records.push_back({
                .type = LightRecordType::Point,
                .index = static_cast<std::uint32_t>(pointLights.size()),
            });
            pointLights.push_back(point->getImpl());
            powers.push_back(point->estimatePower());
            break;
        }
        case LightType::Count: throw kira::Anyhow("LightTableData: unsupported light type");
        }
    }

    for (std::size_t primIndex = 0; primIndex < prims.size(); ++primIndex) {
        auto const &prim = prims[primIndex];
        if (!prim->getEDF())
            continue;
        if (records.size() >= LightPowerDistribution::maxLightCount)
            throw kira::Anyhow("LightTableData: light count exceeds sampler resolution");
        if (prim->hasNonUniformScale())
            LogWarn(
                "LightTableData: emissive primitive {} has non-uniform scale; using its average "
                "scale for light sampling",
                prim->getContextId()
            );

        primImpls[primIndex].lightIndex = static_cast<std::uint32_t>(records.size());
        records.push_back({
            .type = LightRecordType::Primitive,
            .index = static_cast<std::uint32_t>(primIndices.size()),
        });
        primIndices.push_back(static_cast<std::uint32_t>(primIndex));
        primAreaScales.push_back(prim->estimateAreaScale());
        powers.push_back(prim->estimatePower());
    }
}

void LightTableData::clear() noexcept {
    records.clear();
    pointLights.clear();
    primIndices.clear();
    primAreaScales.clear();
    powers.clear();
}

LightTable LightTableData::getTable() const noexcept {
    return {
        .records = records.data(),
        .pointLights = pointLights.data(),
        .primIndices = primIndices.data(),
        .primAreaScales = primAreaScales.data(),
    };
}

} // namespace flux
