#include "flux/Optix/OptixShaderCompiler.h"

#include <cuda_runtime_api.h>
#include <nvrtc.h>
#include <tbb/enumerable_thread_specific.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>

#include "flux/Core/Logging.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Optix/SlangCompiler.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto slangModules = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Optix/OptixSlangModules.inc"
});

constexpr auto nvrtcHeaders = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Optix/OptixNvrtcHeaders.inc"
});

constexpr auto callableModule = "flux_callable";
constexpr auto preludeInclude = "slang-cuda-prelude.h";
constexpr auto shaderInclude = "shader.cu";

/// \brief The name that a Slang import gives the module at \p path.
[[nodiscard]] std::string getModuleName(std::filesystem::path const &path) {
    auto name = path.stem().string();
    std::ranges::replace(name, '-', '_');
    return name;
}

void nvrtcCheck(nvrtcResult result) {
    if (result != NVRTC_SUCCESS)
        throw kira::Anyhow(
            "OptixShaderCompiler: NVRTC call error: {}", nvrtcGetErrorString(result)
        );
}

using NvrtcProgram = std::unique_ptr<_nvrtcProgram, decltype([](nvrtcProgram program) {
    if (nvrtcDestroyProgram(&program) != NVRTC_SUCCESS)
        LogError("OptixShaderCompiler: NVRTC cannot destroy a program");
})>;

[[nodiscard]] std::string makeBridge() {
    return fmt::format(
        R"cuda(#include "{prelude}"
#include "flux/Optix/OptixShaderCallable.h"
#include "flux/Optix/OptixCallableLaunchParams.h"
#include "flux/Optix/OptixImageTexture.cuh"

struct OptixLaunchParamsView {{
    flux::OptixCallableLaunchParams callable;
    char padding[FLUX_LAUNCH_PARAMS_SIZE - sizeof(flux::OptixCallableLaunchParams)];
}};
extern "C" __constant__ OptixLaunchParamsView optixLaunchParams;

namespace flux {{
static __device__ float4
sampleImageTexture(OptixShadingContext const &context, unsigned int slot, float2 uv) {{
    auto const &texture = optixLaunchParams.callable.imageTextures.getTexture(
        optixLaunchParams.callable.textureIndices[context.textureOffset + slot]
    );
    return texture.apply(texture.sample(uv, context.duvdx, context.duvdy));
}}
}}

namespace {{
#include "{shader}"
}}

extern "C" __device__ flux::OptixBSDFResult {bsdfCallable}(
    unsigned int wordOffset, flux::OptixShadingContext context, float3 wo, float3 wi, bool eval,
    float u1, float2 u2
) {{
    auto *words = const_cast<unsigned int *>(optixLaunchParams.callable.words) + wordOffset;
    return fluxExecuteBSDF(words, context, wo, wi, eval, u1, u2);
}}

extern "C" __device__ float3 {edfCallable}(
    unsigned int wordOffset, unsigned int textureOffset, float2 uv, float3 normal, float3 wo
) {{
    auto *words = const_cast<unsigned int *>(optixLaunchParams.callable.words) + wordOffset;
    // The derivatives stay zero, and the geometric normal is the shading normal.
    flux::OptixShadingContext context{{}};
    context.uv = uv;
    context.normal = normal;
    context.geometricNormal = normal;
    context.textureOffset = textureOffset;
    // The tangent of flux::Frame around the normal.
    auto const sign = copysignf(1.0F, normal.z);
    auto const a = -1.0F / (sign + normal.z);
    auto const b = normal.x * normal.y * a;
    context.tangent = make_float3(1.0F + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    return fluxEvaluateEDF(words, context, wo);
}}

template <typename T, typename U> constexpr bool is_same_v = false;
template <typename T> constexpr bool is_same_v<T, T> = true;
static_assert(is_same_v<decltype({bsdfCallable}), flux::OptixBSDFCallable>);
static_assert(is_same_v<decltype({edfCallable}), flux::OptixEDFCallable>);
)cuda",
        fmt::arg("prelude", preludeInclude), fmt::arg("shader", shaderInclude),
        fmt::arg("bsdfCallable", OptixShaderCompiler::bsdfCallableName),
        fmt::arg("edfCallable", OptixShaderCompiler::edfCallableName)
    );
}

/// The name of the NVRTC program, which prefixes the internal symbols of its module.
[[nodiscard]] std::string getProgramName(std::string const &source) {
    return fmt::format("shader_{:016x}.cu", std::hash<std::string>{}(source));
}

[[nodiscard]] std::vector<char> compileToOptixIr(
    std::string const &cuda, std::string const &prelude, std::string const &architectureOption,
    std::string const &programName
) {
    auto const bridge = makeBridge();
    std::vector<char const *> headerNames{preludeInclude, shaderInclude};
    std::vector<char const *> headerTexts{prelude.c_str(), cuda.c_str()};
    for (auto const &[name, text] : nvrtcHeaders) {
        headerNames.push_back(name);
        headerTexts.push_back(text);
    }

    nvrtcProgram handle{};
    nvrtcCheck(nvrtcCreateProgram(
        &handle, bridge.c_str(), programName.c_str(), static_cast<int>(headerNames.size()),
        headerTexts.data(), headerNames.data()
    ));
    auto const program = NvrtcProgram{handle};

    auto const launchParamsSize =
        fmt::format("-DFLUX_LAUNCH_PARAMS_SIZE={}", sizeof(OptixLaunchParams));
    // -pch keeps the bridge's first include, the Slang prelude, parsed for later compiles.
    auto const options = std::array{
        "-optix-ir",       "--std=c++20", architectureOption.c_str(), "--use_fast_math",
        "-default-device", "-rdc=true",   launchParamsSize.c_str(),   "-pch",
    };
    auto const result =
        nvrtcCompileProgram(handle, static_cast<int>(options.size()), options.data());
    if (result != NVRTC_SUCCESS) {
        std::size_t logSize{};
        nvrtcCheck(nvrtcGetProgramLogSize(handle, &logSize));
        std::string log(logSize, '\0');
        nvrtcCheck(nvrtcGetProgramLog(handle, log.data()));
        throw kira::Anyhow(
            "OptixShaderCompiler: NVRTC failed with {}:\n{}", nvrtcGetErrorString(result), log
        );
    }

    std::size_t size{};
    nvrtcCheck(nvrtcGetOptiXIRSize(handle, &size));
    std::vector<char> ir(size);
    nvrtcCheck(nvrtcGetOptiXIR(handle, ir.data()));
    return ir;
}

[[nodiscard]] std::string getArchitectureOption() {
    int device{};
    cudaCheck(cudaGetDevice(&device));
    int major{};
    int minor{};
    cudaCheck(cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, device));
    cudaCheck(cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, device));
    return fmt::format("-arch=compute_{}{}", major, minor);
}

[[nodiscard]] double millisecondsSince(std::chrono::steady_clock::time_point start) noexcept {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}
} // namespace

struct OptixShaderCompiler::pImpl {
    std::string architectureOption{getArchitectureOption()};

    /// One compiler for each thread that compiles.
    tbb::enumerable_thread_specific<SlangCompiler> slangCompilers{[] {
        return SlangCompiler{SlangCompiler::Target::CudaSource};
    }};
};

OptixShaderCompiler::OptixShaderCompiler() : pImpl_(std::make_unique<pImpl>()) {}

OptixShaderCompiler::~OptixShaderCompiler() = default;

std::vector<char> OptixShaderCompiler::compile(std::string const &source) const {
    auto start = std::chrono::steady_clock::now();
    std::vector<SlangModule> modules;
    for (auto const &[path, text] : slangModules) {
        auto name = getModuleName(path);
        if (name == callableModule)
            modules.push_back({.name = "shader", .source = source});
        modules.push_back({.name = std::move(name), .source = text});
    }
    auto const &slangCompiler = pImpl_->slangCompilers.local();
    auto const cuda = slangCompiler.compile(modules, callableModule);
    auto const &prelude = slangCompiler.getTargetPrelude();
    auto const slangMilliseconds = millisecondsSince(start);

    start = std::chrono::steady_clock::now();
    auto ir = compileToOptixIr(cuda, prelude, pImpl_->architectureOption, getProgramName(source));
    auto const nvrtcMilliseconds = millisecondsSince(start);

    LogInfo(
        "OptixShaderCompiler: compiled a shader in {:.1f} ms (Slang {:.1f} ms, NVRTC {:.1f} ms)",
        slangMilliseconds + nvrtcMilliseconds, slangMilliseconds, nvrtcMilliseconds
    );
    return ir;
}
} // namespace flux
