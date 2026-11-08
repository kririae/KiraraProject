#include "flux/Optix/OptixMegaKernelModule.h"

#include <optix_stubs.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <vector>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixPipelineCompileOptions.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Primitive.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] std::vector<char> readBinary(std::filesystem::path const &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw kira::Anyhow("OptixMegaKernelModule: failed to open OptiX IR '{}'", path.string());

    auto const end = input.tellg();
    if (end <= 0)
        throw kira::Anyhow("OptixMegaKernelModule: OptiX IR '{}' is empty", path.string());
    if (end > std::numeric_limits<std::streamsize>::max())
        throw kira::Anyhow("OptixMegaKernelModule: OptiX IR '{}' is too large", path.string());

    std::vector<char> data(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!input.read(data.data(), static_cast<std::streamsize>(data.size())))
        throw kira::Anyhow("OptixMegaKernelModule: failed to read OptiX IR '{}'", path.string());
    return data;
}
} // namespace

OptixMegaKernelModule::OptixMegaKernelModule(
    OptixDeviceContext deviceContext, std::filesystem::path const &modulePath,
    OptixMegaKernelModuleSpec spec
)
    : OptixMegaKernelModule(EmptyState{}, deviceContext, spec) {
    if (!deviceContext_)
        throw kira::Anyhow("OptixMegaKernelModule: device context must not be null");

    buildModule(modulePath);
    buildProgramGroups();
}

OptixMegaKernelModule::OptixMegaKernelModule(
    EmptyState, OptixDeviceContext deviceContext, OptixMegaKernelModuleSpec spec
) noexcept
    : deviceContext_(deviceContext), spec_(spec) {}

OptixMegaKernelModule::~OptixMegaKernelModule() {
    for (auto program = programs_.rbegin(); program != programs_.rend(); ++program)
        if (*program)
            optixCheck<false>(optixProgramGroupDestroy(*program));
    if (module_)
        optixCheck<false>(optixModuleDestroy(module_));
}

OptixMegaKernelModuleSpec OptixMegaKernelModule::makeSpec(Context const &context) {
    auto const integrator = context.getActiveIntegrator();
    auto const sampler = context.getActiveSampler();
    if (!integrator)
        throw kira::Anyhow("OptixMegaKernelModule: context has no active integrator");
    if (!sampler)
        throw kira::Anyhow("OptixMegaKernelModule: context has no active sampler");
    auto bsdfTypes = BSDFTypeMask{};
    for (auto const &primitive : context.getObjects<Primitive>()) {
        if (!primitive->isVisible())
            continue;

        auto const bsdf = primitive->getBSDF();
        if (!bsdf)
            continue;

        bsdfTypes |= bsdfTypeBit(bsdf->getType());
    }

    auto edfTypes = EDFTypeMask{};
    for (auto const &primitive : context.getObjects<Primitive>())
        if (primitive->isLight())
            edfTypes |= edfTypeBit(primitive->getEDF()->getType());

    return {
        .samplerType = sampler->getType(),
        .bsdfTypes = bsdfTypes,
        .edfTypes = edfTypes,
        .shaderReorder = integrator->usesShaderReorder(),
        .hasEnvMap = static_cast<bool>(context.getActiveEnvMap()),
        .chunkSize = integrator->getChunkSize(),
    };
}

void OptixMegaKernelModule::buildModule(std::filesystem::path const &modulePath) {
    auto const ir = readBinary(modulePath);
    OptixModuleCompileOptions moduleOptions{};
    // Keep the megakernel below the measured OptiX register-allocation cliff.
    moduleOptions.maxRegisterCount = megakernelMaxRegisterCount;
    moduleOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_3;
    moduleOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;

    auto const boundValues = std::array{
        OptixModuleCompileBoundValueEntry{
            .pipelineParamOffsetInBytes =
                offsetof(OptixLaunchParams, sampler) + offsetof(Sampler::Impl, type),
            .sizeInBytes = sizeof(spec_.samplerType),
            .boundValuePtr = &spec_.samplerType,
            .annotation = "Active sampler implementation",
        },
        OptixModuleCompileBoundValueEntry{
            .pipelineParamOffsetInBytes =
                offsetof(OptixLaunchParams, bsdfDispatcher) + offsetof(BSDF::Dispatcher, types),
            .sizeInBytes = sizeof(spec_.bsdfTypes),
            .boundValuePtr = &spec_.bsdfTypes,
            .annotation = "Visible BSDF implementation mask",
        },
        OptixModuleCompileBoundValueEntry{
            .pipelineParamOffsetInBytes =
                offsetof(OptixLaunchParams, edfDispatcher) + offsetof(EDF::Dispatcher, types),
            .sizeInBytes = sizeof(spec_.edfTypes),
            .boundValuePtr = &spec_.edfTypes,
            .annotation = "Visible EDF implementation mask",
        },
        OptixModuleCompileBoundValueEntry{
            .pipelineParamOffsetInBytes = offsetof(OptixLaunchParams, shaderReorder),
            .sizeInBytes = sizeof(spec_.shaderReorder),
            .boundValuePtr = &spec_.shaderReorder,
            .annotation = "Enable SER for surface hits",
        },
        OptixModuleCompileBoundValueEntry{
            .pipelineParamOffsetInBytes = offsetof(OptixLaunchParams, hasEnvMap),
            .sizeInBytes = sizeof(spec_.hasEnvMap),
            .boundValuePtr = &spec_.hasEnvMap,
            .annotation = "Active environment map presence",
        },
        OptixModuleCompileBoundValueEntry{
            .pipelineParamOffsetInBytes = offsetof(OptixLaunchParams, chunkSize),
            .sizeInBytes = sizeof(spec_.chunkSize),
            .boundValuePtr = &spec_.chunkSize,
            .annotation = "Paths per chunk",
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
    optixCheck(result, log, logSize);
}

void OptixMegaKernelModule::buildProgramGroups() {
    auto const raygen = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .raygen = {
            .module = module_,
            .entryFunctionName = "__raygen__megakernel",
        },
    };
    programs_[raygenProgram] = createProgramGroup(deviceContext_, raygen);

    auto const miss = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_MISS,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .miss = {
            .module = module_,
            .entryFunctionName = "__miss__megakernel",
        },
    };
    programs_[missProgram] = createProgramGroup(deviceContext_, miss);

    auto const triangle = OptixProgramGroupDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .hitgroup = {},
    };
    programs_[hitgroupPrograms + static_cast<std::size_t>(GeometryType::TriangleMesh)] =
        createProgramGroup(deviceContext_, triangle);
}
} // namespace flux
