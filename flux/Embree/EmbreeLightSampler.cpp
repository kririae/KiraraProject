#include "flux/Embree/EmbreeLightSampler.h"

#include <cstdint>

#include "flux/Core/KIRA.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
void EmbreeLightSampler::build(
    std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> primitives,
    std::span<Primitive::Impl> primitiveImpls
) {
    if (lights.size() > LightSampler::maxLightCount)
        throw kira::Anyhow("EmbreeLightSampler: light count exceeds sampler resolution");
    if (primitiveImpls.size() != primitives.size())
        throw kira::Anyhow("EmbreeLightSampler: primitive tables do not match");

    clear();
    records_.reserve(lights.size() + primitives.size());
    pointLights_.reserve(lights.size());
    primitiveIndices_.reserve(primitives.size());
    primitiveAreaScales_.reserve(primitives.size());
    std::vector<float> powers;
    powers.reserve(lights.size() + primitives.size());

    for (auto const &light : lights) {
        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "EmbreeLightSampler: light type does not match its host implementation"
                );
            records_.push_back({
                .type = LightRecordType::Point,
                .typedIndex = static_cast<std::uint32_t>(pointLights_.size()),
            });
            pointLights_.push_back(point->getImpl());
            powers.push_back(point->estimatePower());
            break;
        }
        case LightType::Count: throw kira::Anyhow("EmbreeLightSampler: unsupported light type");
        }
    }

    for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
        auto const &primitive = primitives[primitiveIndex];
        if (!primitive->getEDF())
            continue;
        if (records_.size() >= LightSampler::maxLightCount)
            throw kira::Anyhow("EmbreeLightSampler: light count exceeds sampler resolution");
        if (primitive->hasNonUniformScale())
            LogWarn(
                "EmbreeLightSampler: emissive primitive {} uses approximate area scaling",
                primitive->getContextId()
            );

        primitiveImpls[primitiveIndex].lightIndex = static_cast<std::uint32_t>(records_.size());
        records_.push_back({
            .type = LightRecordType::Primitive,
            .typedIndex = static_cast<std::uint32_t>(primitiveIndices_.size()),
        });
        primitiveIndices_.push_back(static_cast<std::uint32_t>(primitiveIndex));
        primitiveAreaScales_.push_back(primitive->estimateAreaScale());
        powers.push_back(primitive->estimatePower());
    }
    powerCDF_ = buildLightPowerCDF(powers);
}

void EmbreeLightSampler::clear() noexcept {
    records_.clear();
    pointLights_.clear();
    primitiveIndices_.clear();
    primitiveAreaScales_.clear();
    powerCDF_.clear();
}

LightSampler EmbreeLightSampler::getSampler() const noexcept {
    return {
        .lights = {
            .records = records_.data(),
            .pointLights = pointLights_.data(),
            .primitiveIndices = primitiveIndices_.data(),
            .primitiveAreaScales = primitiveAreaScales_.data(),
            .powerCDF = powerCDF_.data(),
            .powerSum = powerCDF_.empty() ? 0.0F : powerCDF_.back(),
            .numLights = static_cast<std::uint32_t>(records_.size()),
        },
    };
}
} // namespace flux
