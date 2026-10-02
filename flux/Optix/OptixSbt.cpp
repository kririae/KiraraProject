#include "flux/Optix/OptixSbt.h"

#include <optix_stubs.h>

#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
void OptixSbt::build(OptixProgram const &program) {
    optixCheck(optixSbtRecordPackHeader(program.getRaygenProgram(), &staging_[raygenRecord]));
    optixCheck(optixSbtRecordPackHeader(program.getMissProgram(), &staging_[missRecord]));

    for (std::size_t bsdf = 0; bsdf < numBSDFTypes; ++bsdf) {
        auto const bsdfType = static_cast<BSDFType>(bsdf);
        for (std::size_t geometry = 0; geometry < numGeometryTypes; ++geometry) {
            auto const geometryType = static_cast<GeometryType>(geometry);
            auto &record =
                staging_[hitgroupRecords + getHitgroupRecordIndex(bsdfType, geometryType)];
            optixCheck(optixSbtRecordPackHeader(program.getHitgroupProgram(geometryType), &record));
        }
    }

    records_.copyFromHost(staging_);

    table_ = {
        .raygenRecord = devicePointer(records_.data() + raygenRecord),
        .exceptionRecord = 0,
        .missRecordBase = devicePointer(records_.data() + missRecord),
        .missRecordStrideInBytes = sizeof(Record),
        .missRecordCount = 1,
        .hitgroupRecordBase = devicePointer(records_.data() + hitgroupRecords),
        .hitgroupRecordStrideInBytes = sizeof(Record),
        .hitgroupRecordCount = static_cast<unsigned int>(numHitgroupRecords),
        .callablesRecordBase = 0,
        .callablesRecordStrideInBytes = 0,
        .callablesRecordCount = 0,
    };
}
} // namespace flux
