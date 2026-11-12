#pragma once

#include <optix_types.h>

namespace flux {
/// \brief Returns the pipeline compile options that every module of a pipeline shares.
[[nodiscard]] inline OptixPipelineCompileOptions pipelineCompileOptions() {
    OptixPipelineCompileOptions options{};
    options.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    options.numPayloadValues = 0;
    options.numAttributeValues = 2;
    options.pipelineLaunchParamsVariableName = "optixLaunchParams";
    options.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    return options;
}
} // namespace flux
