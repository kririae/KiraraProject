#include "flux/Optix/OptixContext.h"

#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_host.h>
#include <optix_stubs.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "flux/Core/KIRA.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
namespace {
/// \brief Shader binding table record for the ray-generation program.
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) RaygenRecord {
    std::array<char, OPTIX_SBT_RECORD_HEADER_SIZE> header;
};

[[nodiscard]] std::vector<char> readBinary(std::filesystem::path const &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw kira::Anyhow("OptixContext: failed to open OptiX IR '{}'", path.string());

    auto const end = input.tellg();
    if (end <= 0)
        throw kira::Anyhow("OptixContext: OptiX IR '{}' is empty", path.string());
    if (end > std::numeric_limits<std::streamsize>::max())
        throw kira::Anyhow("OptixContext: OptiX IR '{}' is too large", path.string());

    std::vector<char> data(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!input.read(data.data(), static_cast<std::streamsize>(data.size())))
        throw kira::Anyhow("OptixContext: failed to read OptiX IR '{}'", path.string());
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
} // namespace

struct OptixContext::Impl {
    Impl(
        Context &hostContext, OptixDeviceContext optixDeviceContext,
        std::filesystem::path const &modulePath
    );
    ~Impl() { reset(); }

    /// \brief Builds the ray-generation pipeline from \p modulePath.
    void buildPipeline(std::filesystem::path const &modulePath);

    /// \brief Builds and uploads the shader binding table.
    void buildSbt();

    /// \brief Releases every owned resource without throwing.
    void reset() noexcept;

    /// Host scene mirrored by this object. Its lifetime is owned by the handler.
    Context *context{};

    /// OptiX device context borrowed from the handler.
    OptixDeviceContext deviceContext{};

    /// Compiled OptiX IR module.
    OptixModule module{};

    /// Ray-generation program group built from \c module.
    OptixProgramGroup raygenProgram{};

    /// Pipeline containing \c raygenProgram.
    OptixPipeline pipeline{};

    /// Device allocation backing the ray-generation SBT record.
    void *raygenRecord{};

    /// Shader binding table passed to each launch.
    OptixShaderBindingTable sbt{};
};

OptixContext::Impl::Impl(
    Context &hostContext, OptixDeviceContext optixDeviceContext,
    std::filesystem::path const &modulePath
)
    : context(&hostContext), deviceContext(optixDeviceContext) {
    if (!optixDeviceContext)
        throw kira::Anyhow("OptixContext: device context must not be null");

    try {
        buildPipeline(modulePath);
        buildSbt();
    } catch (...) {
        reset();
        throw;
    }
}

void OptixContext::Impl::buildPipeline(std::filesystem::path const &modulePath) {
    auto const ir = readBinary(modulePath);

    OptixModuleCompileOptions moduleOptions{};
    OptixPipelineCompileOptions pipelineOptions{};
    pipelineOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;

    std::array<char, 4096> log{};
    std::size_t logSize = log.size();
    // clang-format off
    auto result = optixModuleCreate(
        /* context =                */ deviceContext,
        /* moduleCompileOptions =   */ &moduleOptions,
        /* pipelineCompileOptions = */ &pipelineOptions,
        /* input =                  */ ir.data(),
        /* inputSize =              */ ir.size(),
        /* logString =              */ log.data(),
        /* logStringSize =          */ &logSize,
        /* module =                 */ &module);
    // clang-format on
    logCompilerOutput(result, log, logSize);
    optixCheck(result);

    OptixProgramGroupOptions programOptions{};
    OptixProgramGroupDesc programDesc{
        .kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN,
        .flags = OPTIX_PROGRAM_GROUP_FLAGS_NONE,
        .raygen = {
            .module = module,
            .entryFunctionName = "__raygen__megakernel",
        },
    };
    log.fill('\0');
    logSize = log.size();
    // clang-format off
    result = optixProgramGroupCreate(
        /* context =             */ deviceContext,
        /* programDescriptions = */ &programDesc,
        /* numProgramGroups =    */ 1,
        /* options =             */ &programOptions,
        /* logString =           */ log.data(),
        /* logStringSize =       */ &logSize,
        /* programGroups =       */ &raygenProgram);
    // clang-format on
    logCompilerOutput(result, log, logSize);
    optixCheck(result);

    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 0;
    linkOptions.maxTraversableGraphDepth = 1;
    log.fill('\0');
    logSize = log.size();
    // clang-format off
    result = optixPipelineCreate(
        /* context =                */ deviceContext,
        /* pipelineCompileOptions = */ &pipelineOptions,
        /* pipelineLinkOptions =    */ &linkOptions,
        /* programGroups =          */ &raygenProgram,
        /* numProgramGroups =       */ 1,
        /* logString =              */ log.data(),
        /* logStringSize =          */ &logSize,
        /* pipeline =               */ &pipeline);
    // clang-format on
    logCompilerOutput(result, log, logSize);
    optixCheck(result);

    // clang-format off
    optixCheck(optixPipelineSetStackSizeFromCallDepths(
        /* pipeline =                            */ pipeline,
        /* maxTraceDepth =                       */ 0,
        /* maxContinuationCallableDepth =        */ 0,
        /* maxDirectCallableDepthFromState =     */ 0,
        /* maxDirectCallableDepthFromTraversal = */ 0,
        /* maxTraversableGraphDepth =            */ 1));
    // clang-format on
}

void OptixContext::Impl::buildSbt() {
    RaygenRecord record{};
    optixCheck(optixSbtRecordPackHeader(raygenProgram, &record));
    cudaCheck(cudaMalloc(&raygenRecord, sizeof(record)));
    cudaCheck(cudaMemcpy(raygenRecord, &record, sizeof(record), cudaMemcpyHostToDevice));
    sbt.raygenRecord = reinterpret_cast<CUdeviceptr>(raygenRecord);
}

void OptixContext::Impl::reset() noexcept {
    if (raygenRecord)
        cudaCheck<false>(cudaFree(raygenRecord));
    if (pipeline)
        optixCheck<false>(optixPipelineDestroy(pipeline));
    if (raygenProgram)
        optixCheck<false>(optixProgramGroupDestroy(raygenProgram));
    if (module)
        optixCheck<false>(optixModuleDestroy(module));

    raygenRecord = nullptr;
    pipeline = nullptr;
    raygenProgram = nullptr;
    module = nullptr;
    deviceContext = nullptr;
    context = nullptr;
}

OptixContext::OptixContext(
    Context &context, OptixDeviceContext deviceContext, std::filesystem::path const &modulePath
)
    : impl_(std::make_unique<Impl>(context, deviceContext, modulePath)) {}

OptixContext::~OptixContext() = default;

OptixPipeline OptixContext::getPipeline() const noexcept { return impl_->pipeline; }

OptixShaderBindingTable const &OptixContext::getSbt() const noexcept { return impl_->sbt; }
} // namespace flux
