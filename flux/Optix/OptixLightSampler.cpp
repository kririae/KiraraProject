#include "flux/Optix/OptixLightSampler.h"

namespace flux {
void OptixLightSampler::build(
    std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> prims,
    std::span<Primitive::Impl> primImpls
) {
    records_.clear();
    pointLights_.clear();
    primIndices_.clear();
    primAreaScales_.clear();
    powerCDF_.clear();

    staging_.build(lights, prims, primImpls);
    powerCDFStaging_ = buildLightPowerCDF(staging_.powers);
    records_.copyFromHost(staging_.records);
    pointLights_.copyFromHost(staging_.pointLights);
    primIndices_.copyFromHost(staging_.primIndices);
    primAreaScales_.copyFromHost(staging_.primAreaScales);
    powerCDF_.copyFromHost(powerCDFStaging_);
}

OptixLightSampler::Impl OptixLightSampler::getImpl() const noexcept {
    return {
        .table =
            {
                .records = records_.data(),
                .pointLights = pointLights_.data(),
                .primIndices = primIndices_.data(),
                .primAreaScales = primAreaScales_.data(),
            },
        .power = {
            .cdf = powerCDF_.data(),
            .sum = powerCDFStaging_.empty() ? 0.0F : powerCDFStaging_.back(),
            .numLights = static_cast<std::uint32_t>(records_.size()),
        },
    };
}
} // namespace flux
