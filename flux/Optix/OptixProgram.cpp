#include "flux/Optix/OptixProgram.h"

#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/Optix/OptixPipelineCompileOptions.h"
#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
OptixProgram::OptixProgram(
    OptixDeviceContext deviceContext, std::shared_ptr<OptixMegaKernelModule const> megaKernelModule,
    std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules
)
    : OptixProgram(
          EmptyState{}, deviceContext, std::move(megaKernelModule), std::move(shaderModules)
      ) {
    if (!deviceContext_)
        throw kira::Anyhow("OptixProgram: device context must not be null");

    buildPipeline();
}

OptixProgram::OptixProgram(
    EmptyState, OptixDeviceContext deviceContext,
    std::shared_ptr<OptixMegaKernelModule const> megaKernelModule,
    std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules
) noexcept
    : deviceContext_(deviceContext), megaKernelModule_(std::move(megaKernelModule)),
      shaderModules_(std::move(shaderModules)) {}

OptixProgram::~OptixProgram() {
    if (pipeline_)
        optixCheck<false>(optixPipelineDestroy(pipeline_));
}

void OptixProgram::buildPipeline() {
    auto const pipelineOptions = pipelineCompileOptions();
    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 1;
    linkOptions.maxTraversableGraphDepth = 2;

    auto const megaKernelPrograms = megaKernelModule_->getProgramGroups();
    std::vector<OptixProgramGroup> programs(megaKernelPrograms.begin(), megaKernelPrograms.end());
    for (auto const &module : shaderModules_)
        programs.push_back(module->getProgramGroup());
    std::array<char, 4096> log{};
    std::size_t logSize = log.size();
    // clang-format off
    auto const result = optixPipelineCreate(
        /* context =                */ deviceContext_,
        /* pipelineCompileOptions = */ &pipelineOptions,
        /* pipelineLinkOptions =    */ &linkOptions,
        /* programGroups =          */ programs.data(),
        /* numProgramGroups =       */ static_cast<unsigned int>(programs.size()),
        /* logString =              */ log.data(),
        /* logStringSize =          */ &logSize,
        /* pipeline =               */ &pipeline_);
    // clang-format on
    optixCheck(result, log, logSize);

    OptixStackSizes stackSizes{};
    for (auto *const program : programs)
        optixCheck(optixUtilAccumulateStackSizes(program, &stackSizes, pipeline_));
    LogDebug(
        "OptiX stack sizes (bytes): raygen={}, miss={}, closest-hit={}, any-hit={}, "
        "intersection={}, continuation-callable={}, direct-callable={}",
        stackSizes.cssRG, stackSizes.cssMS, stackSizes.cssCH, stackSizes.cssAH, stackSizes.cssIS,
        stackSizes.cssCC, stackSizes.dssDC
    );

    // clang-format off
    optixCheck(optixPipelineSetStackSizeFromCallDepths(
        /* pipeline =                            */ pipeline_,
        /* maxTraceDepth =                       */ 1,
        /* maxContinuationCallableDepth =        */ 0,
        /* maxDirectCallableDepthFromState =     */ 1,
        /* maxDirectCallableDepthFromTraversal = */ 0,
        /* maxTraversableGraphDepth =            */ 2));
    // clang-format on
}
} // namespace flux
