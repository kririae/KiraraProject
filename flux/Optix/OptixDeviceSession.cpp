#include "flux/Optix/OptixDeviceSession.h"

#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <algorithm>
#include <string_view>

#include "flux/Core/Logging.h"
#include "flux/Optix/OptixMegaKernelModule.h"
#include "flux/Optix/OptixUtils.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
/// \brief Estimates how many megakernel threads \p properties keeps resident.
[[nodiscard]] std::uint32_t estimateResidentThreads(cudaDeviceProp const &properties) noexcept {
    auto const perSM = std::min(
        properties.maxThreadsPerMultiProcessor,
        static_cast<int>(properties.regsPerMultiprocessor / megakernelMaxRegisterCount)
    );
    return static_cast<std::uint32_t>(properties.multiProcessorCount) *
           static_cast<std::uint32_t>(std::max(perSM, 1));
}
} // namespace

OptixDeviceSession::OptixDeviceSession(int deviceOrdinal) : OptixDeviceSession(EmptyState{}) {
    cuCheck(cuInit(0));
    int deviceCount = 0;
    cuCheck(cuDeviceGetCount(&deviceCount));
    if (deviceCount == 0)
        throw kira::Anyhow("OptixDeviceSession: no CUDA device is available");
    if (deviceOrdinal < 0 || deviceOrdinal >= deviceCount)
        throw kira::Anyhow(
            "OptixDeviceSession: CUDA device {} is outside [0, {})", deviceOrdinal, deviceCount
        );

    CUdevice device{};
    cuCheck(cuDeviceGet(&device, deviceOrdinal));
    cuCheck(cuDevicePrimaryCtxRetain(&primaryContext_, device));
    device_ = device;

    CudaContextGuard const guard(primaryContext_);
    cudaDeviceProp deviceProperties{};
    cudaCheck(cudaGetDeviceProperties(&deviceProperties, deviceOrdinal));
    LogInfo(
        "OptixDeviceSession: using CUDA device {} ('{}')", deviceOrdinal, deviceProperties.name
    );
    estimatedResidentThreads_ = estimateResidentThreads(deviceProperties);
    cudaCheck(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
    optixCheck(optixInit());

    OptixDeviceContextOptions options{};
    optixCheck(optixDeviceContextCreate(nullptr, &options, &context_));
    optixCheck(optixDeviceContextSetLogCallback(
        context_, +[](unsigned int level, char const *tag, char const *message, void *) {
        auto text = std::string_view{message};
        while (text.ends_with('\n'))
            text.remove_suffix(1);
        if (text.empty())
            return;

        // A failed build call returns its compiler errors through its log buffer, and the
        // exception it raises carries them, so logging them here would report them twice.
        // Compiler warnings reach only this callback: a successful build discards the buffer.
        if (level <= 2 && std::string_view{tag} == "COMPILER")
            return;

        switch (level) {
        case 1:
        case 2: LogError("OptiX [{}]: {}", tag, text); break;
        case 3: LogWarn("OptiX [{}]: {}", tag, text); break;
        default: LogDebug("OptiX [{}]: {}", tag, text); break;
        }
    }, nullptr, 4
    ));
}

OptixDeviceSession::~OptixDeviceSession() {
    if (!primaryContext_)
        return;

    {
        CudaContextGuard const guard(primaryContext_, std::nothrow);
        if (stream_)
            cudaCheck<false>(cudaStreamSynchronize(stream_));
        if (context_)
            optixCheck<false>(optixDeviceContextDestroy(context_));
        if (stream_)
            cudaCheck<false>(cudaStreamDestroy(stream_));
    }
    cuCheck<false>(cuDevicePrimaryCtxRelease(device_));
}
} // namespace flux
