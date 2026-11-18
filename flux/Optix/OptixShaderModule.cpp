#include "flux/Optix/OptixShaderModule.h"

#include <optix_stubs.h>

#include <array>
#include <cstddef>
#include <stdexcept>

#include "flux/Optix/OptixPipelineCompileOptions.h"
#include "flux/Optix/OptixShaderCompiler.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
OptixShaderModule::OptixShaderModule(OptixDeviceContext deviceContext, std::span<char const> ir)
    : OptixShaderModule(EmptyState{}) {
    if (!deviceContext)
        throw std::invalid_argument("OptixShaderModule: device context must not be null");

    OptixModuleCompileOptions moduleOptions{};
    moduleOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_3;
    moduleOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
    auto const pipelineOptions = pipelineCompileOptions();
    std::array<char, 4096> log{};
    std::size_t logSize = log.size();

    // clang-format off
    auto const result = optixModuleCreate(
        /* context =                */ deviceContext,
        /* moduleCompileOptions =   */ &moduleOptions,
        /* pipelineCompileOptions = */ &pipelineOptions,
        /* input =                  */ ir.data(),
        /* inputSize =              */ ir.size(),
        /* logString =              */ log.data(),
        /* logStringSize =          */ &logSize,
        /* module =                 */ &module_);
    // clang-format on
    optixCheck(result, log, logSize);

    auto const bsdfCallable = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .callables = {
            .moduleDC = module_,
            .entryFunctionNameDC = OptixShaderCompiler::bsdfCallableName,
            .moduleCC = nullptr,
            .entryFunctionNameCC = nullptr,
        },
    };
    bsdfProgram_ = createProgramGroup(deviceContext, bsdfCallable);

    auto const edfCallable = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .callables = {
            .moduleDC = module_,
            .entryFunctionNameDC = OptixShaderCompiler::edfCallableName,
            .moduleCC = nullptr,
            .entryFunctionNameCC = nullptr,
        },
    };
    edfProgram_ = createProgramGroup(deviceContext, edfCallable);
}

OptixShaderModule::~OptixShaderModule() {
    if (edfProgram_)
        optixCheck<false>(optixProgramGroupDestroy(edfProgram_));
    if (bsdfProgram_)
        optixCheck<false>(optixProgramGroupDestroy(bsdfProgram_));
    if (module_)
        optixCheck<false>(optixModuleDestroy(module_));
}
} // namespace flux
