#include "flux/Embree/EmbreeLightSampler.h"

#include <cstdint>

#include "kira/Anyhow.h"

namespace flux {
void EmbreeLightSampler::build(std::span<Ref<Light const> const> lights) {
    if (lights.size() > LightSampler::maxLightCount)
        throw kira::Anyhow("EmbreeLightSampler: light count exceeds sampler resolution");

    clear();
    records_.reserve(lights.size());
    pointLights_.reserve(lights.size());

    for (auto const &light : lights) {
        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "EmbreeLightSampler: light type does not match its host implementation"
                );
            records_.push_back({
                .type = LightType::Point,
                .typedIndex = static_cast<std::uint32_t>(pointLights_.size()),
            });
            pointLights_.push_back(point->getImpl());
            break;
        }
        case LightType::Count: throw kira::Anyhow("EmbreeLightSampler: unsupported light type");
        }
    }
}

void EmbreeLightSampler::clear() noexcept {
    records_.clear();
    pointLights_.clear();
}

LightSampler EmbreeLightSampler::getSampler() const noexcept {
    return {
        .lights = {
            .records = records_.data(),
            .pointLights = pointLights_.data(),
            .numLights = static_cast<std::uint32_t>(records_.size()),
        },
    };
}
} // namespace flux
