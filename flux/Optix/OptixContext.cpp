#include "flux/Optix/OptixContext.h"

#include <optix_stubs.h>

#include <array>
#include <vector>

#include "flux/Geometry/TriangleMesh.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixAccel.h"
#include "flux/Optix/OptixGeometryPool.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"

namespace flux {
namespace {
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord {
    std::array<char, OPTIX_SBT_RECORD_HEADER_SIZE> header;
};
} // namespace

struct OptixContext::Impl {
    struct SbtStaging {
        SbtRecord raygen{};
        SbtRecord miss{};
        std::vector<SbtRecord> hitgroups;
    };

    Impl(
        OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path const &modulePath
    )
        : deviceContext(deviceContext), stream(stream), program(deviceContext, modulePath),
          geometryPool(stream), accel(deviceContext, stream), raygenRecord(stream),
          missRecord(stream), hitgroupRecords(stream) {}

    void buildSbt(SbtStaging &staging) {
        optixCheck(optixSbtRecordPackHeader(program.getRaygenProgram(), &staging.raygen));
        optixCheck(optixSbtRecordPackHeader(program.getMissProgram(), &staging.miss));

        staging.hitgroups.resize(geometryPool.size());
        for (auto &record : staging.hitgroups)
            optixCheck(optixSbtRecordPackHeader(program.getHitgroupProgram(), &record));

        raygenRecord.copyFromHost({&staging.raygen, 1});
        missRecord.copyFromHost({&staging.miss, 1});
        hitgroupRecords.copyFromHost({staging.hitgroups.data(), staging.hitgroups.size()});

        sbt = {
            .raygenRecord = devicePointer(raygenRecord.data()),
            .exceptionRecord = 0,
            .missRecordBase = devicePointer(missRecord.data()),
            .missRecordStrideInBytes = sizeof(SbtRecord),
            .missRecordCount = 1,
            .hitgroupRecordBase = devicePointer(hitgroupRecords.data()),
            .hitgroupRecordStrideInBytes =
                hitgroupRecords.empty() ? 0U : static_cast<unsigned int>(sizeof(SbtRecord)),
            .hitgroupRecordCount = static_cast<unsigned int>(hitgroupRecords.size()),
            .callablesRecordBase = 0,
            .callablesRecordStrideInBytes = 0,
            .callablesRecordCount = 0,
        };
    }

    void sync(Context &context) {
        context.commit();

        SbtStaging staging;
        try {
            hitgroupRecords.clear();
            missRecord.clear();
            raygenRecord.clear();

            auto const meshes = context.getObjects<TriangleMesh>();
            geometryPool.upload(meshes);
            auto const buildInputs = geometryPool.getBuildInputs();
            accel.build(buildInputs);
            buildSbt(staging);
            cudaCheck(cudaStreamSynchronize(stream));
        } catch (...) {
            cudaCheck<false>(cudaStreamSynchronize(stream));
            throw;
        }
    }

    OptixDeviceContext deviceContext;
    cudaStream_t stream;
    OptixProgram program;
    OptixGeometryPool geometryPool;
    OptixAccel accel;
    DeviceBuffer<SbtRecord> raygenRecord;
    DeviceBuffer<SbtRecord> missRecord;
    DeviceBuffer<SbtRecord> hitgroupRecords;
    OptixShaderBindingTable sbt{};
};

OptixContext::OptixContext(
    OptixDeviceContext deviceContext, cudaStream_t stream, std::filesystem::path const &modulePath
)
    : impl_(std::make_unique<Impl>(deviceContext, stream, modulePath)) {}

OptixContext::~OptixContext() = default;

void OptixContext::sync(Context &context) { impl_->sync(context); }

void OptixContext::launch(
    cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t width
) const {
    // clang-format off
    optixCheck(optixLaunch(
        /* pipeline =           */ impl_->program.getPipeline(),
        /* stream =             */ stream,
        /* pipelineParams =     */ params,
        /* pipelineParamsSize = */ paramsSize,
        /* sbt =                */ &impl_->sbt,
        /* width =              */ width,
        /* height =             */ 1,
        /* depth =              */ 1));
    // clang-format on
}

OptixTraversableHandle OptixContext::getTraversable() const noexcept {
    return impl_->accel.getHandle();
}
} // namespace flux
