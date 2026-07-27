#include "flux/Optix/OptixAccel.h"

#include <optix_stubs.h>

#include <limits>

#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
void OptixAccel::build(std::span<OptixBuildInput const> inputs) {
    if (inputs.empty()) {
        output_.clear();
        handle_ = {};
        return;
    }
    if (inputs.size() > std::numeric_limits<unsigned int>::max())
        throw kira::Anyhow("OptixAccel: build input count exceeds OptiX limits");

    OptixAccelBuildOptions const options{
        .buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE,
        .operation = OPTIX_BUILD_OPERATION_BUILD,
        .motionOptions = {},
    };

    OptixAccelBufferSizes sizes{};
    optixCheck(optixAccelComputeMemoryUsage(
        deviceContext_, &options, inputs.data(), static_cast<unsigned int>(inputs.size()), &sizes
    ));

    output_.clear();
    handle_ = {};

    DeviceBuffer<std::byte> temporary(getStream());
    temporary.resize(sizes.tempSizeInBytes);
    output_.resize(sizes.outputSizeInBytes);

    // clang-format off
    optixCheck(optixAccelBuild(
        /* context =              */ deviceContext_,
        /* stream =               */ getStream(),
        /* accelOptions =         */ &options,
        /* buildInputs =          */ inputs.data(),
        /* numBuildInputs =       */ static_cast<unsigned int>(inputs.size()),
        /* tempBuffer =           */ devicePointer(temporary.data()),
        /* tempBufferSizeInBytes = */ temporary.size(),
        /* outputBuffer =         */ devicePointer(output_.data()),
        /* outputBufferSizeInBytes = */ output_.size(),
        /* outputHandle =         */ &handle_,
        /* emittedProperties =    */ nullptr,
        /* numEmittedProperties = */ 0));
    // clang-format on
}
} // namespace flux
