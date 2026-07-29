#include "flux/Optix/OptixLightSampler.h"

#include <cstdint>

#include "kira/Anyhow.h"

namespace flux {
void OptixLightSampler::build(std::span<Ref<Light const> const> lights) {
    if (lights.size() > LightSampler::maxLightCount)
        throw kira::Anyhow("OptixLightSampler: light count exceeds sampler resolution");

    records_.clear();
    pointLights_.clear();
    recordStaging_.clear();
    pointLightStaging_.clear();
    recordStaging_.reserve(lights.size());
    pointLightStaging_.reserve(lights.size());

    for (auto const &light : lights) {
        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "OptixLightSampler: light type does not match its host implementation"
                );
            recordStaging_.push_back({
                .type = LightType::Point,
                .typedIndex = static_cast<std::uint32_t>(pointLightStaging_.size()),
            });
            pointLightStaging_.push_back(point->getImpl());
            break;
        }
        case LightType::Count: throw kira::Anyhow("OptixLightSampler: unsupported light type");
        }
    }

    records_.copyFromHost(recordStaging_);
    pointLights_.copyFromHost(pointLightStaging_);
}

LightSampler OptixLightSampler::getSampler() const noexcept {
    return {
        .lights = {
            .records = records_.data(),
            .pointLights = pointLights_.data(),
            .numLights = static_cast<std::uint32_t>(records_.size()),
        },
    };
}
} // namespace flux
