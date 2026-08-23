#include "flux/Embree/EmbreeLightSampler.h"

namespace flux {
void EmbreeLightSampler::build(
    std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> prims,
    std::span<Primitive::Impl> primImpls
) {
    tableData_.build(lights, prims, primImpls);
    powerCDF_ = buildLightPowerCDF(tableData_.powers);
}

void EmbreeLightSampler::clear() noexcept {
    tableData_.clear();
    powerCDF_.clear();
}

EmbreeLightSampler::Impl EmbreeLightSampler::getImpl() const noexcept {
    return {
        .table = tableData_.getTable(),
        .power = {
            .cdf = powerCDF_.data(),
            .sum = powerCDF_.empty() ? 0.0F : powerCDF_.back(),
            .numLights = static_cast<std::uint32_t>(tableData_.records.size()),
        },
    };
}
} // namespace flux
