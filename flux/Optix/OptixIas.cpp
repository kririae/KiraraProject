#include "flux/Optix/OptixIas.h"

#include <optix_stubs.h>

#include <algorithm>
#include <cstdint>

#include "flux/Core/Math.h"
#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
void OptixIas::build(OptixDeviceContext deviceContext, std::span<InstanceDesc const> instances) {
    clear();

    if (instances.empty())
        return;
    auto const maxInstances =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_INSTANCES_PER_IAS);
    auto const maxInstanceId =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_INSTANCE_ID);
    auto const maxSbtOffset =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_SBT_OFFSET);
    if (instances.size() > maxInstances)
        throw kira::Anyhow("OptixIas: instance count exceeds the device limit");

    instanceStaging_.reserve(instances.size());
    for (std::size_t index = 0; index < instances.size(); ++index) {
        auto const &description = instances[index];
        if (!description.gas)
            throw kira::Anyhow("OptixIas: instance references a geometry without a GAS");
        if (description.primitiveIndex > maxInstanceId)
            throw kira::Anyhow("OptixIas: primitive index exceeds the device instance ID limit");
        if (description.sbtOffset > maxSbtOffset)
            throw kira::Anyhow("OptixIas: instance SBT offset exceeds the device limit");

        OptixInstance instance{};
        std::ranges::copy(description.transform, instance.transform);
        instance.instanceId = description.primitiveIndex;
        instance.sbtOffset = description.sbtOffset;
        instance.visibilityMask = 255;
        instance.flags = OPTIX_INSTANCE_FLAG_NONE;
        instance.traversableHandle = description.gas;
        instanceStaging_.push_back(instance);
    }

    instances_.copyFromHost({instanceStaging_.data(), instanceStaging_.size()});

    OptixBuildInput input{
        .type = OPTIX_BUILD_INPUT_TYPE_INSTANCES,
        .instanceArray = {
            .instances = devicePointer(instances_.data()),
            .numInstances = static_cast<unsigned int>(instances_.size()),
            .instanceStride = 0,
        },
    };
    OptixAccelBuildOptions const options{
        .buildFlags =
            OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_RANDOM_INSTANCE_ACCESS,
        .operation = OPTIX_BUILD_OPERATION_BUILD,
        .motionOptions = {},
    };
    OptixAccelBufferSizes sizes{};
    optixCheck(optixAccelComputeMemoryUsage(deviceContext, &options, &input, 1, &sizes));

    DeviceBuffer<std::byte> temporary(getStream());
    temporary.resize(sizes.tempSizeInBytes);
    ias_.resize(sizes.outputSizeInBytes);
    iasBounds_.resize(1);
    OptixAccelEmitDesc const bounds{
        .result = devicePointer(iasBounds_.data()),
        .type = OPTIX_PROPERTY_TYPE_AABBS,
    };

    // clang-format off
    optixCheck(optixAccelBuild(
        /* context =                 */ deviceContext,
        /* stream =                  */ getStream(),
        /* accelOptions =            */ &options,
        /* buildInputs =             */ &input,
        /* numBuildInputs =          */ 1,
        /* tempBuffer =              */ devicePointer(temporary.data()),
        /* tempBufferSizeInBytes =   */ temporary.size(),
        /* outputBuffer =            */ devicePointer(ias_.data()),
        /* outputBufferSizeInBytes = */ ias_.size(),
        /* outputHandle =            */ &handle_,
        /* emittedProperties =       */ &bounds,
        /* numEmittedProperties =    */ 1));
    // clang-format on

    iasBounds_.copyToHost({&iasBoundsStaging_, 1});
    cudaCheck(cudaStreamSynchronize(getStream()));
}

void OptixIas::clear() noexcept {
    instanceStaging_.clear();
    instances_.clear();
    ias_.clear();
    iasBounds_.clear();
    iasBoundsStaging_ = {};
    handle_ = {};
}

float OptixIas::getSceneRadius() const noexcept {
    if (!handle_)
        return 1.0F;
    return Vec3f{
               iasBoundsStaging_.maxX - iasBoundsStaging_.minX,
               iasBoundsStaging_.maxY - iasBoundsStaging_.minY,
               iasBoundsStaging_.maxZ - iasBoundsStaging_.minZ,
           }
               .norm() *
           0.5F;
}
} // namespace flux
