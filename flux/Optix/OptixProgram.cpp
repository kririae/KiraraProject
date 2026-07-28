#include "flux/Optix/OptixProgram.h"

#include <optix_stubs.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "flux/Core/KIRA.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
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
    options.numPayloadValues = 2;
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
    OptixDeviceContext deviceContext, std::filesystem::path const &modulePath, OptixProgramSpec spec
)
    : deviceContext_(deviceContext), spec_(spec) {
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

OptixProgramSpec OptixProgram::makeSpec(Context const &context) {
    // The active integrator selects this path-tracing program. PathIntegrator
    // has no specialization values.
    (void)context.getActiveIntegrator();
    return {
        .samplerType = context.getActiveSampler()->getType(), // (1)
    };
}

void OptixProgram::buildModule(std::filesystem::path const &modulePath) {
    auto const ir = readBinary(modulePath);
    OptixModuleCompileOptions moduleOptions{};

    // Keep these entries in the numbered order used by OptixProgramSpec.
    auto const boundValues = std::array{
        OptixModuleCompileBoundValueEntry{
            // (1)
            .pipelineParamOffsetInBytes =
                offsetof(OptixLaunchParams, sampler) + offsetof(Sampler::DeviceImpl, type),
            .sizeInBytes = sizeof(spec_.samplerType),
            .boundValuePtr = &spec_.samplerType,
            .annotation = "Flux sampler implementation",
        },
    };
    moduleOptions.boundValues = boundValues.data();
    moduleOptions.numBoundValues = static_cast<unsigned int>(boundValues.size());
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

    auto const radianceMiss = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_MISS,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .miss = {
            .module = module_,
            .entryFunctionName = "__miss__radiance",
        },
    };
    missPrograms_[static_cast<std::size_t>(RayType::Radiance)] =
        createProgramGroup(deviceContext_, radianceMiss);

    auto const shadowMiss = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_MISS,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .miss = {
            .module = module_,
            .entryFunctionName = "__miss__shadow",
        },
    };
    missPrograms_[static_cast<std::size_t>(RayType::Shadow)] =
        createProgramGroup(deviceContext_, shadowMiss);

    OptixProgramGroupDesc diffuseTriangle{};
    diffuseTriangle.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    diffuseTriangle.hitgroup.moduleCH = module_;
    diffuseTriangle.hitgroup.entryFunctionNameCH = "__closesthit__triangle_diffuse";
    radianceHitgroupPrograms_[OptixSbt::getHitgroupBlock(
        BSDFType::Diffuse, GeometryType::TriangleMesh
    )] = createProgramGroup(deviceContext_, diffuseTriangle);

    OptixProgramGroupDesc triangleShadow{};
    triangleShadow.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    triangleShadow.hitgroup.moduleCH = module_;
    triangleShadow.hitgroup.entryFunctionNameCH = "__closesthit__triangle_shadow";
    shadowHitgroupPrograms_[static_cast<std::size_t>(GeometryType::TriangleMesh)] =
        createProgramGroup(deviceContext_, triangleShadow);
}

void OptixProgram::buildPipeline() {
    auto const pipelineOptions = pipelineCompileOptions();
    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 1;
    linkOptions.maxTraversableGraphDepth = 2;

    std::vector<OptixProgramGroup> programs;
    programs.reserve(
        1 + missPrograms_.size() + radianceHitgroupPrograms_.size() + shadowHitgroupPrograms_.size()
    );
    programs.push_back(raygenProgram_);
    programs.insert(programs.end(), missPrograms_.begin(), missPrograms_.end());
    programs.insert(
        programs.end(), radianceHitgroupPrograms_.begin(), radianceHitgroupPrograms_.end()
    );
    programs.insert(programs.end(), shadowHitgroupPrograms_.begin(), shadowHitgroupPrograms_.end());
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
    for (auto &program : shadowHitgroupPrograms_)
        if (program)
            optixCheck<false>(optixProgramGroupDestroy(program));
    for (auto &program : radianceHitgroupPrograms_)
        if (program)
            optixCheck<false>(optixProgramGroupDestroy(program));
    for (auto &program : missPrograms_)
        if (program)
            optixCheck<false>(optixProgramGroupDestroy(program));
    if (raygenProgram_)
        optixCheck<false>(optixProgramGroupDestroy(raygenProgram_));
    if (module_)
        optixCheck<false>(optixModuleDestroy(module_));

    pipeline_ = nullptr;
    shadowHitgroupPrograms_.fill(nullptr);
    radianceHitgroupPrograms_.fill(nullptr);
    missPrograms_.fill(nullptr);
    raygenProgram_ = nullptr;
    module_ = nullptr;
    deviceContext_ = nullptr;
}
} // namespace flux
