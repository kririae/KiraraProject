#include "flux/Optix/OptixLightSampler.h"

#include <cstdint>

#include "flux/Core/KIRA.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

namespace flux {
void OptixLightSampler::build(
    std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> primitives,
    std::span<Primitive::Impl> primitiveImpls
) {
    if (lights.size() > LightSampler::maxLightCount)
        throw kira::Anyhow("OptixLightSampler: light count exceeds sampler resolution");
    if (primitiveImpls.size() != primitives.size())
        throw kira::Anyhow("OptixLightSampler: primitive tables do not match");

    records_.clear();
    pointLights_.clear();
    primitiveIndices_.clear();
    primitiveAreaScales_.clear();
    powerCDF_.clear();
    recordStaging_.clear();
    pointLightStaging_.clear();
    primitiveIndexStaging_.clear();
    primitiveAreaScaleStaging_.clear();
    powerCDFStaging_.clear();
    recordStaging_.reserve(lights.size() + primitives.size());
    pointLightStaging_.reserve(lights.size());
    primitiveIndexStaging_.reserve(primitives.size());
    primitiveAreaScaleStaging_.reserve(primitives.size());
    std::vector<float> powers;
    powers.reserve(lights.size() + primitives.size());

    for (auto const &light : lights) {
        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "OptixLightSampler: light type does not match its host implementation"
                );
            recordStaging_.push_back({
                .type = LightRecordType::Point,
                .typedIndex = static_cast<std::uint32_t>(pointLightStaging_.size()),
            });
            pointLightStaging_.push_back(point->getImpl());
            powers.push_back(point->estimatePower());
            break;
        }
        case LightType::Count: throw kira::Anyhow("OptixLightSampler: unsupported light type");
        }
    }

    for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
        auto const &primitive = primitives[primitiveIndex];
        if (!primitive->getEDF())
            continue;
        if (recordStaging_.size() >= LightSampler::maxLightCount)
            throw kira::Anyhow("OptixLightSampler: light count exceeds sampler resolution");
        if (primitive->hasNonUniformScale())
            LogWarn(
                "OptixLightSampler: emissive primitive {} uses approximate area scaling",
                primitive->getContextId()
            );

        primitiveImpls[primitiveIndex].lightIndex =
            static_cast<std::uint32_t>(recordStaging_.size());
        recordStaging_.push_back({
            .type = LightRecordType::Primitive,
            .typedIndex = static_cast<std::uint32_t>(primitiveIndexStaging_.size()),
        });
        primitiveIndexStaging_.push_back(static_cast<std::uint32_t>(primitiveIndex));
        primitiveAreaScaleStaging_.push_back(primitive->estimateAreaScale());
        powers.push_back(primitive->estimatePower());
    }

    powerCDFStaging_ = buildLightPowerCDF(powers);

    records_.copyFromHost(recordStaging_);
    pointLights_.copyFromHost(pointLightStaging_);
    primitiveIndices_.copyFromHost(primitiveIndexStaging_);
    primitiveAreaScales_.copyFromHost(primitiveAreaScaleStaging_);
    powerCDF_.copyFromHost(powerCDFStaging_);
}

LightSampler OptixLightSampler::getSampler() const noexcept {
    return {
        .lights = {
            .records = records_.data(),
            .pointLights = pointLights_.data(),
            .primitiveIndices = primitiveIndices_.data(),
            .primitiveAreaScales = primitiveAreaScales_.data(),
            .powerCDF = powerCDF_.data(),
            .powerSum = powerCDFStaging_.empty() ? 0.0F : powerCDFStaging_.back(),
            .numLights = static_cast<std::uint32_t>(records_.size()),
        },
    };
}
} // namespace flux
