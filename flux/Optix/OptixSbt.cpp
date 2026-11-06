#include "flux/Optix/OptixSbt.h"

#include <optix_stubs.h>

#include <vector>

#include "flux/Optix/OptixMegaKernelModule.h"
#include "flux/Optix/OptixShaderModule.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
void OptixSbt::build(
    OptixMegaKernelModule const &module,
    std::span<std::shared_ptr<OptixShaderModule const> const> shaderModules
) {
    std::vector<Record> records(callablesRecords + shaderModules.size());
    optixCheck(optixSbtRecordPackHeader(module.getRaygenProgram(), &records[raygenRecord]));
    optixCheck(optixSbtRecordPackHeader(module.getMissProgram(), &records[missRecord]));

    for (std::size_t bsdf = 0; bsdf < numBSDFTypes; ++bsdf) {
        auto const bsdfType = static_cast<BSDFType>(bsdf);
        for (std::size_t geometry = 0; geometry < numGeometryTypes; ++geometry) {
            auto const geometryType = static_cast<GeometryType>(geometry);
            auto &record =
                records[hitgroupRecords + getHitgroupRecordIndex(bsdfType, geometryType)];
            optixCheck(optixSbtRecordPackHeader(module.getHitgroupProgram(geometryType), &record));
        }
    }

    for (std::size_t slot = 0; slot < shaderModules.size(); ++slot)
        optixCheck(optixSbtRecordPackHeader(
            shaderModules[slot]->getProgramGroup(), &records[callablesRecords + slot]
        ));

    records_.copyFromHost(records, deletionQueue_);

    table_ = {
        .raygenRecord = devicePointer(records_.data() + raygenRecord),
        .exceptionRecord = 0,
        .missRecordBase = devicePointer(records_.data() + missRecord),
        .missRecordStrideInBytes = sizeof(Record),
        .missRecordCount = 1,
        .hitgroupRecordBase = devicePointer(records_.data() + hitgroupRecords),
        .hitgroupRecordStrideInBytes = sizeof(Record),
        .hitgroupRecordCount = static_cast<unsigned int>(numHitgroupRecords),
        .callablesRecordBase =
            shaderModules.empty() ? 0 : devicePointer(records_.data() + callablesRecords),
        .callablesRecordStrideInBytes = sizeof(Record),
        .callablesRecordCount = static_cast<unsigned int>(shaderModules.size()),
    };
}
} // namespace flux
