#include "flux/Optix/OptixProgram.h"

#include <optix_stubs.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "flux/Core/KIRA.h"
#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] std::vector<char> readBinary(std::filesystem::path const &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw kira::Anyhow("OptixProgram: failed to open OptiX IR '{}'", path.string());

    auto const end = input.tellg();
    if (end <= 0)
        throw kira::Anyhow("OptixProgram: OptiX IR '{}' is empty", path.string());
    if (end > std::numeric_limits<std::streamsize>::max())
        throw kira::Anyhow("OptixProgram: OptiX IR '{}' is too large", path.string());

    std::vector<char> data(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!input.read(data.data(), static_cast<std::streamsize>(data.size())))
        throw kira::Anyhow("OptixProgram: failed to read OptiX IR '{}'", path.string());
    return data;
}

void logCompilerOutput(OptixResult result, std::array<char, 4096> const &log, std::size_t logSize) {
    if (logSize <= 1)
        return;

    auto const output = std::string_view(log.data(), std::min(logSize - 1, log.size()));
    if (result == OPTIX_SUCCESS)
        LogDebug("OptiX compiler output:\n{}", output);
    else
        LogError("OptiX compiler output:\n{}", output);
}

[[nodiscard]] OptixPipelineCompileOptions pipelineCompileOptions() {
    OptixPipelineCompileOptions options{};
    options.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    options.numPayloadValues = 5;
    options.numAttributeValues = 2;
    options.pipelineLaunchParamsVariableName = "optixLaunchParams";
    options.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    return options;
}

[[nodiscard]] OptixProgramGroup
createProgramGroup(OptixDeviceContext deviceContext, OptixProgramGroupDesc const &description) {
    OptixProgramGroupOptions options{};
    OptixProgramGroup program{};
    std::array<char, 4096> log{};
    std::size_t logSize = log.size();
    // clang-format off
    auto const result = optixProgramGroupCreate(
        /* context =             */ deviceContext,
        /* programDescriptions = */ &description,
        /* numProgramGroups =    */ 1,
        /* options =             */ &options,
        /* logString =           */ log.data(),
        /* logStringSize =       */ &logSize,
        /* programGroups =       */ &program);
    // clang-format on
    logCompilerOutput(result, log, logSize);
    optixCheck(result);
    return program;
}
} // namespace

OptixProgram::OptixProgram(
    OptixDeviceContext deviceContext, std::filesystem::path const &modulePath
)
    : deviceContext_(deviceContext) {
    if (!deviceContext_)
        throw kira::Anyhow("OptixProgram: device context must not be null");

    try {
        buildModule(modulePath);
        buildProgramGroups();
        buildPipeline();
    } catch (...) {
        reset();
        throw;
    }
}

OptixProgram::~OptixProgram() { reset(); }

void OptixProgram::buildModule(std::filesystem::path const &modulePath) {
    auto const ir = readBinary(modulePath);
    OptixModuleCompileOptions moduleOptions{};
    auto const pipelineOptions = pipelineCompileOptions();
    std::array<char, 4096> log{};
    std::size_t logSize = log.size();

    // clang-format off
    auto const result = optixModuleCreate(
        /* context =                */ deviceContext_,
        /* moduleCompileOptions =   */ &moduleOptions,
        /* pipelineCompileOptions = */ &pipelineOptions,
        /* input =                  */ ir.data(),
        /* inputSize =              */ ir.size(),
        /* logString =              */ log.data(),
        /* logStringSize =          */ &logSize,
        /* module =                 */ &module_);
    // clang-format on
    logCompilerOutput(result, log, logSize);
    optixCheck(result);
}

void OptixProgram::buildProgramGroups() {
    auto const raygen = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .raygen = {
            .module = module_,
            .entryFunctionName = "__raygen__megakernel",
        },
    };
    raygenProgram_ = createProgramGroup(deviceContext_, raygen);

    auto const miss = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_MISS,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .miss = {
            .module = module_,
            .entryFunctionName = "__miss__intersection",
        },
    };
    missProgram_ = createProgramGroup(deviceContext_, miss);

    OptixProgramGroupDesc hitgroup{};
    hitgroup.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitgroup.hitgroup.moduleCH = module_;
    hitgroup.hitgroup.entryFunctionNameCH = "__closesthit__triangle";
    hitgroupProgram_ = createProgramGroup(deviceContext_, hitgroup);
}

void OptixProgram::buildPipeline() {
    auto const pipelineOptions = pipelineCompileOptions();
    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 1;
    linkOptions.maxTraversableGraphDepth = 2;

    std::array const programs{raygenProgram_, missProgram_, hitgroupProgram_};
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
    logCompilerOutput(result, log, logSize);
    optixCheck(result);

    // clang-format off
    optixCheck(optixPipelineSetStackSizeFromCallDepths(
        /* pipeline =                            */ pipeline_,
        /* maxTraceDepth =                       */ 1,
        /* maxContinuationCallableDepth =        */ 0,
        /* maxDirectCallableDepthFromState =     */ 0,
        /* maxDirectCallableDepthFromTraversal = */ 0,
        /* maxTraversableGraphDepth =            */ 2));
    // clang-format on
}

void OptixProgram::reset() noexcept {
    if (pipeline_)
        optixCheck<false>(optixPipelineDestroy(pipeline_));
    if (hitgroupProgram_)
        optixCheck<false>(optixProgramGroupDestroy(hitgroupProgram_));
    if (missProgram_)
        optixCheck<false>(optixProgramGroupDestroy(missProgram_));
    if (raygenProgram_)
        optixCheck<false>(optixProgramGroupDestroy(raygenProgram_));
    if (module_)
        optixCheck<false>(optixModuleDestroy(module_));

    pipeline_ = nullptr;
    hitgroupProgram_ = nullptr;
    missProgram_ = nullptr;
    raygenProgram_ = nullptr;
    module_ = nullptr;
    deviceContext_ = nullptr;
}
} // namespace flux
