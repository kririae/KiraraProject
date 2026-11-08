#include <cuda_runtime_api.h>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <optix_stubs.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "flux/Optix/OptixMegaKernelModule.h"
#include "flux/Optix/OptixPipelineCompileOptions.h"
#include "flux/Optix/OptixShaderCompiler.h"
#include "flux/Optix/OptixShaderModule.h"
#include "flux/Optix/OptixShaderPool.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/ShaderAsset.h"

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

constexpr auto shaderSource = R"slang(
import flux_material;
import flux_context;

public IBSDF makeBSDF(uint* words, ShadingContext ctx)
{
    return makeMixBSDF(EmptyBSDF(), EmptyBSDF(), asfloat(words[0]));
}

public IEDF makeEDF(uint* words, ShadingContext ctx) { return EmptyEDF(); }
)slang";

/// Calls a CUDA function that the compiler keeps as a call.
constexpr auto shaderSourceWithCall = R"slang(
import flux_material;
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

public IEDF makeEDF(uint* words, ShadingContext ctx) { return EmptyEDF(); }
)slang";
} // namespace

TEST(OptixShaderModule, HasABSDFProgramGroup) {
    Device const device;
    flux::OptixShaderCompiler const compiler;

    auto const ir = compiler.compile(shaderSource);
    flux::OptixShaderModule const module(device.context, ir);

    EXPECT_NE(module.getBSDFProgramGroup(), nullptr);
}

TEST(OptixShaderModule, TwoSourcesThatDifferOnlyByACommentLinkIntoOnePipeline) {
    Device const device;
    flux::OptixShaderCompiler const compiler;

    auto const firstIr = compiler.compile(shaderSourceWithCall);
    auto const secondIr =
        compiler.compile(std::string{shaderSourceWithCall} + "// Another source.\n");
    flux::OptixShaderModule const first(device.context, firstIr);
    flux::OptixShaderModule const second(device.context, secondIr);

    flux::OptixMegaKernelModule const megaKernel(
        device.context, std::filesystem::path(FLUX_TEST_OPTIX_IR),
        {.samplerType = flux::SamplerType::Independent,
         .bsdfTypes = 0,
         .edfTypes = 0,
         .shaderReorder = false,
         .hasEnvMap = false,
         .chunkSize = 1}
    );
    auto const megaKernelGroups = megaKernel.getProgramGroups();
    std::vector<OptixProgramGroup> groups{megaKernelGroups.begin(), megaKernelGroups.end()};
    groups.push_back(first.getBSDFProgramGroup());
    groups.push_back(second.getBSDFProgramGroup());

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

TEST(OptixShaderPool, AddBuildsAModuleForEachOfSeveralShaders) {
    Device const device;
    flux::ShaderAssetPool assetPool;
    std::vector<flux::Ref<flux::ShaderAsset const>> assets;
    for (auto const word : {0, 1, 2, 3})
        assets.push_back(assetPool.getOrCreate(
            {.source = fmt::format(
                 "import flux_material;\nimport flux_context;\n"
                 "public IBSDF makeBSDF(uint* words, ShadingContext ctx)\n"
                 "{{ return makeMixBSDF(EmptyBSDF(), EmptyBSDF(), asfloat(words[{}])); }}\n"
                 "public IEDF makeEDF(uint* words, ShadingContext ctx) {{ return EmptyEDF(); }}\n",
                 word
             )}
        ));

    flux::OptixShaderPool pool{device.context};
    pool.add({assets.data(), assets.size()});

    std::set<OptixProgramGroup> groups;
    for (auto const &asset : assets) {
        auto const module = pool.getModule(asset->getId());
        EXPECT_NE(module->getBSDFProgramGroup(), nullptr);
        groups.insert(module->getBSDFProgramGroup());
    }
    EXPECT_EQ(groups.size(), assets.size());
}
