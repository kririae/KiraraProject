#include "flux/Optix/OptixSbt.h"

#include <optix_stubs.h>

#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
void OptixSbt::build(OptixProgram const &program) {
    optixCheck(optixSbtRecordPackHeader(program.getRaygenProgram(), &staging_[raygenRecord]));

    for (std::size_t ray = 0; ray < numRayTypes; ++ray) {
        auto const rayType = static_cast<RayType>(ray);
        optixCheck(
            optixSbtRecordPackHeader(program.getMissProgram(rayType), &staging_[missRecords + ray])
        );
    }

    for (std::size_t bsdf = 0; bsdf < numBSDFTypes; ++bsdf) {
        auto const bsdfType = static_cast<BSDFType>(bsdf);
        for (std::size_t geometry = 0; geometry < numGeometryTypes; ++geometry) {
            auto const geometryType = static_cast<OptixGeometryType>(geometry);
            auto const radiance =
                hitgroupRecords + getHitgroupRecord(bsdfType, geometryType, RayType::Radiance);
            auto const shadow =
                hitgroupRecords + getHitgroupRecord(bsdfType, geometryType, RayType::Shadow);
            optixCheck(optixSbtRecordPackHeader(
                program.getRadianceHitgroupProgram(bsdfType, geometryType), &staging_[radiance]
            ));
            optixCheck(optixSbtRecordPackHeader(
                program.getShadowHitgroupProgram(geometryType), &staging_[shadow]
            ));
        }
    }

    records_.copyFromHost(staging_);

    table_ = {
        .raygenRecord = devicePointer(records_.data() + raygenRecord),
        .exceptionRecord = 0,
        .missRecordBase = devicePointer(records_.data() + missRecords),
        .missRecordStrideInBytes = sizeof(Record),
        .missRecordCount = static_cast<unsigned int>(numMissRecords),
        .hitgroupRecordBase = devicePointer(records_.data() + hitgroupRecords),
        .hitgroupRecordStrideInBytes = sizeof(Record),
        .hitgroupRecordCount = static_cast<unsigned int>(numHitgroupRecords),
        .callablesRecordBase = 0,
        .callablesRecordStrideInBytes = 0,
        .callablesRecordCount = 0,
    };
}
} // namespace flux
