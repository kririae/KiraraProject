#include "flux/Optix/OptixSbt.h"

#include <optix_stubs.h>

#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
void OptixSbt::build(OptixProgram const &program) {
    optixCheck(optixSbtRecordPackHeader(program.getRaygenProgram(), &staging_[RaygenRecord]));
    optixCheck(optixSbtRecordPackHeader(program.getMissProgram(), &staging_[MissRecord]));
    optixCheck(optixSbtRecordPackHeader(program.getHitgroupProgram(), &staging_[HitgroupRecord]));

    records_.copyFromHost(staging_);

    table_ = {
        .raygenRecord = devicePointer(records_.data() + RaygenRecord),
        .exceptionRecord = 0,
        .missRecordBase = devicePointer(records_.data() + MissRecord),
        .missRecordStrideInBytes = sizeof(Record),
        .missRecordCount = 1,
        .hitgroupRecordBase = devicePointer(records_.data() + HitgroupRecord),
        .hitgroupRecordStrideInBytes = sizeof(Record),
        .hitgroupRecordCount = 1,
        .callablesRecordBase = 0,
        .callablesRecordStrideInBytes = 0,
        .callablesRecordCount = 0,
    };
}
} // namespace flux
