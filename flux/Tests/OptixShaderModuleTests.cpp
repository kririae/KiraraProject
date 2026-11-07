#include <cuda_runtime_api.h>
#include <gtest/gtest.h>
#include <optix_stubs.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "flux/Optix/OptixBSDFCompiler.h"
#include "flux/Optix/OptixMegaKernelModule.h"
#include "flux/Optix/OptixPipelineCompileOptions.h"
#include "flux/Optix/OptixShaderModule.h"
#include "flux/Optix/OptixUtils.h"

namespace {
/// Owns an OptiX context for one test.
class Device {
public:
    Device() {
        flux::cudaCheck(cudaFree(nullptr));
        flux::optixCheck(optixInit());
        OptixDeviceContextOptions options{};
        flux::optixCheck(optixDeviceContextCreate(nullptr, &options, &context));
    }
    Device(Device const &) = delete;
    Device &operator=(Device const &) = delete;
    ~Device() { flux::optixCheck<false>(optixDeviceContextDestroy(context)); }

    OptixDeviceContext context{};
};

constexpr auto material = R"slang(
import flux_bsdf;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    let normal = fluxToShadingFrame(ctx, fluxGetWorldNormal(ctx));
    return makeOrenNayarDiffuseBSDF(float3(asfloat(words[0])), 0.5, normal);
}
)slang";

/// Calls a CUDA function that the compiler keeps as a call.
constexpr auto materialWithCall = R"slang(
import flux_bsdf;
import flux_context;

float sharedValue(float x)
{
    __requirePrelude("__device__ __noinline__ float sharedValueCall(float x) { asm volatile(\"\" ::: \"memory\"); return sinf(x) * 0.5f + x; }");
    __intrinsic_asm "sharedValueCall($0)";
}

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    return makeMixBSDF(EmptyBSDF(), EmptyBSDF(), sharedValue(asfloat(words[0])));
}
)slang";
} // namespace

TEST(OptixShaderModule, BecomesAProgramGroupFromAMaterial) {
    Device const device;
    flux::OptixBSDFCompiler const compiler;

    auto const ir = compiler.compile(material);
    flux::OptixShaderModule const module(device.context, ir);

    EXPECT_NE(module.getProgramGroup(), nullptr);
}

TEST(OptixShaderModule, TwoSourcesThatDifferOnlyByACommentLinkIntoOnePipeline) {
    Device const device;
    flux::OptixBSDFCompiler const compiler;

    auto const firstIr = compiler.compile(materialWithCall);
    auto const secondIr = compiler.compile(std::string{materialWithCall} + "// Another source.\n");
    flux::OptixShaderModule const first(device.context, firstIr);
    flux::OptixShaderModule const second(device.context, secondIr);

    flux::OptixMegaKernelModule const megaKernel(
        device.context, std::filesystem::path(FLUX_TEST_OPTIX_IR),
        {.samplerType = flux::SamplerType::Independent,
         .bsdfTypes = 0,
         .shaderReorder = false,
         .hasEnvMap = false,
         .chunkSize = 1}
    );
    auto const megaKernelGroups = megaKernel.getProgramGroups();
    std::vector<OptixProgramGroup> groups{megaKernelGroups.begin(), megaKernelGroups.end()};
    groups.push_back(first.getProgramGroup());
    groups.push_back(second.getProgramGroup());

    auto const pipelineOptions = flux::pipelineCompileOptions();
    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 1;
    OptixPipeline pipeline{};
    auto const result = optixPipelineCreate(
        device.context, &pipelineOptions, &linkOptions, groups.data(),
        static_cast<unsigned int>(groups.size()), nullptr, nullptr, &pipeline
    );

    EXPECT_EQ(result, OPTIX_SUCCESS);
    if (pipeline)
        flux::optixCheck<false>(optixPipelineDestroy(pipeline));
}
