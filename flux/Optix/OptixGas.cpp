#include "flux/Optix/OptixGas.h"

#include <optix_stubs.h>

#include <cstdint>

#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
std::vector<OptixGas> buildGas(
    OptixDeviceContext deviceContext, cudaStream_t stream, std::span<OptixBuildInput const> inputs
) {
    std::vector<OptixGas> result;
    if (inputs.empty())
        return result;

    auto const maxPrimitives =
        getDeviceLimit(deviceContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_PRIMITIVES_PER_GAS);
    for (auto const &input : inputs) {
        if (input.type != OPTIX_BUILD_INPUT_TYPE_TRIANGLES)
            throw kira::Anyhow("OptixGas: only triangle build inputs are supported");
        if (input.triangleArray.numIndexTriplets > maxPrimitives)
            throw kira::Anyhow("OptixGas: triangle count exceeds the device limit");
    }

    OptixAccelBuildOptions const options{
        .buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE,
        .operation = OPTIX_BUILD_OPERATION_BUILD,
        .motionOptions = {},
    };

    // Build every GAS, emitting its compacted size.
    DeviceBuffer<std::uint64_t> compactedSizes(stream);
    compactedSizes.resize(inputs.size());
    std::vector<OptixGas> built;
    built.reserve(inputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        auto &gas = built.emplace_back(stream);
        auto const &input = inputs[index];
        OptixAccelBufferSizes sizes{};
        optixCheck(optixAccelComputeMemoryUsage(deviceContext, &options, &input, 1, &sizes));

        DeviceBuffer<std::byte> temporary(stream);
        temporary.resize(sizes.tempSizeInBytes);
        gas.storage.resize(sizes.outputSizeInBytes);

        OptixAccelEmitDesc const compactedSize{
            .result = devicePointer(compactedSizes.data() + index),
            .type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE,
        };

        // clang-format off
        optixCheck(optixAccelBuild(
            /* context =                 */ deviceContext,
            /* stream =                  */ stream,
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
    cudaCheck(cudaStreamSynchronize(stream));

    // Compact every GAS that shrinks, and keep the rest as built.
    result.reserve(inputs.size());
    for (std::size_t index = 0; index < built.size(); ++index) {
        auto &gas = built[index];
        auto &compacted = result.emplace_back(stream);
        if (compactedBytes[index] > 0 && compactedBytes[index] < gas.storage.size()) {
            compacted.storage.resize(static_cast<std::size_t>(compactedBytes[index]));
            optixCheck(optixAccelCompact(
                deviceContext, stream, gas.handle, devicePointer(compacted.storage.data()),
                compacted.storage.size(), &compacted.handle
            ));
        } else {
            compacted.handle = gas.handle;
            compacted.storage = std::move(gas.storage);
        }
    }
    return result;
}
} // namespace flux
