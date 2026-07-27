#include "flux/Optix/OptixAccel.h"

#include <optix_stubs.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] unsigned int
getDeviceLimit(OptixDeviceContext context, OptixDeviceProperty property) {
    unsigned int limit{};
    optixCheck(optixDeviceContextGetProperty(context, property, &limit, sizeof(limit)));
    return limit;
}
} // namespace

void OptixAccel::buildGas(
    OptixDeviceContext deviceContext, std::span<OptixBuildInput const> inputs
) {
    instances_.clear();
    ias_.clear();
    handle_ = {};
    gasEntries_.clear();

    if (inputs.empty())
        return;
    if (inputs.size() > std::numeric_limits<unsigned int>::max())
        throw kira::Anyhow("OptixAccel: build input count exceeds OptiX limits");

    auto const maxPrimitives =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_PRIMITIVES_PER_GAS);
    for (auto const &input : inputs) {
        if (input.type != OPTIX_BUILD_INPUT_TYPE_TRIANGLES)
            throw kira::Anyhow("OptixAccel: only triangle build inputs are supported");
        if (input.triangleArray.numIndexTriplets > maxPrimitives)
            throw kira::Anyhow("OptixAccel: triangle count exceeds the device limit");
    }

    OptixAccelBuildOptions const options{
        .buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE,
        .operation = OPTIX_BUILD_OPERATION_BUILD,
        .motionOptions = {},
    };

    DeviceBuffer<std::uint64_t> compactedSizes(getStream());
    compactedSizes.resize(inputs.size());
    std::vector<GasEntry> pending;
    pending.reserve(inputs.size());
    gasEntries_.reserve(inputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        auto const &input = inputs[index];
        OptixAccelBufferSizes sizes{};
        optixCheck(optixAccelComputeMemoryUsage(deviceContext, &options, &input, 1, &sizes));

        DeviceBuffer<std::byte> temporary(getStream());
        temporary.resize(sizes.tempSizeInBytes);
        auto &gas = pending.emplace_back(getStream());
        gas.storage.resize(sizes.outputSizeInBytes);

        OptixAccelEmitDesc const compactedSize{
            .result = devicePointer(compactedSizes.data() + index),
            .type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE,
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
            /* outputBuffer =            */ devicePointer(gas.storage.data()),
            /* outputBufferSizeInBytes = */ gas.storage.size(),
            /* outputHandle =            */ &gas.handle,
            /* emittedProperties =       */ &compactedSize,
            /* numEmittedProperties =    */ 1));
        // clang-format on
    }

    std::vector<std::uint64_t> compactedBytes(inputs.size());
    compactedSizes.copyToHost({compactedBytes.data(), compactedBytes.size()});
    cudaCheck(cudaStreamSynchronize(getStream()));

    for (std::size_t index = 0; index < pending.size(); ++index) {
        auto &gas = pending[index];
        auto &entry = gasEntries_.emplace_back(getStream());
        if (compactedBytes[index] > 0 && compactedBytes[index] < gas.storage.size()) {
            entry.storage.resize(static_cast<std::size_t>(compactedBytes[index]));
            optixCheck(optixAccelCompact(
                deviceContext, getStream(), gas.handle, devicePointer(entry.storage.data()),
                entry.storage.size(), &entry.handle
            ));
        } else {
            entry.handle = gas.handle;
            entry.storage = std::move(gas.storage);
        }
    }
}

void OptixAccel::buildIas(
    OptixDeviceContext deviceContext, std::span<InstanceDesc const> instances
) {
    instances_.clear();
    ias_.clear();
    handle_ = {};
    instanceStaging_.clear();

    if (instances.empty())
        return;
    auto const maxInstances =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_INSTANCES_PER_IAS);
    auto const maxInstanceId =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_INSTANCE_ID);
    if (instances.size() > maxInstances || instances.size() - 1 > maxInstanceId)
        throw kira::Anyhow("OptixAccel: instance count exceeds the device limit");

    instanceStaging_.reserve(instances.size());
    for (std::size_t index = 0; index < instances.size(); ++index) {
        auto const &description = instances[index];
        if (description.geometryIndex >= gasEntries_.size())
            throw kira::Anyhow("OptixAccel: instance references an unknown geometry");

        OptixInstance instance{};
        std::ranges::copy(description.transform, instance.transform);
        instance.instanceId = static_cast<unsigned int>(index);
        instance.sbtOffset = 0;
        instance.visibilityMask = 255;
        instance.flags = OPTIX_INSTANCE_FLAG_NONE;
        instance.traversableHandle = gasEntries_[description.geometryIndex].handle;
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
        .buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE,
        .operation = OPTIX_BUILD_OPERATION_BUILD,
        .motionOptions = {},
    };
    OptixAccelBufferSizes sizes{};
    optixCheck(optixAccelComputeMemoryUsage(deviceContext, &options, &input, 1, &sizes));

    DeviceBuffer<std::byte> temporary(getStream());
    temporary.resize(sizes.tempSizeInBytes);
    ias_.resize(sizes.outputSizeInBytes);

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
        /* emittedProperties =       */ nullptr,
        /* numEmittedProperties =    */ 0));
    // clang-format on
}
} // namespace flux
