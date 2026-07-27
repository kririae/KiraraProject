#include "flux/Optix/OptixHandler.h"

#include <cuda_runtime_api.h>
#include <optix_stubs.h>

#include <utility>

#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "kira/Anyhow.h"

namespace flux {
struct OptixHandler::Impl {
    Impl(Ref<Context> hostContext, std::filesystem::path const &modulePath);
    ~Impl() { reset(); }

    /// \brief Selects a CUDA device and initializes the OptiX API.
    void initializeRuntime();

    /// \brief Creates the OptiX device context owned by this handler.
    void buildDeviceContext();

    /// \brief Creates the stream used for device-scene updates and launches.
    void buildStream();

    /// \brief Creates the persistent device scene.
    void buildOptixContext(std::filesystem::path const &modulePath);

    /// \brief Releases backend resources in dependency order without throwing.
    void reset() noexcept;

    /// Host scene retained for the lifetime of the backend.
    Ref<Context> context;

    OptixDeviceContext deviceContext{};
    cudaStream_t stream{};

    /// Device scene destroyed before the stream and device context.
    std::unique_ptr<OptixContext> optixContext;
};

OptixHandler::Impl::Impl(Ref<Context> hostContext, std::filesystem::path const &modulePath)
    : context(std::move(hostContext)) {
    if (!context)
        throw kira::Anyhow("OptixHandler: context must not be null");

    try {
        initializeRuntime();
        buildDeviceContext();
        buildStream();
        buildOptixContext(modulePath);
    } catch (...) {
        reset();
        throw;
    }
}

void OptixHandler::Impl::initializeRuntime() {
    int deviceCount = 0;
    cudaCheck(cudaGetDeviceCount(&deviceCount));
    if (deviceCount == 0)
        throw kira::Anyhow("OptixHandler: no CUDA device is available");

    // ponytail: use device 0 until the renderer exposes multi-GPU selection.
    cudaCheck(cudaSetDevice(0));
    optixCheck(optixInit());
}

void OptixHandler::Impl::buildDeviceContext() {
    OptixDeviceContextOptions options{};
    optixCheck(optixDeviceContextCreate(nullptr, &options, &deviceContext));
}

void OptixHandler::Impl::buildStream() {
    cudaCheck(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
}

void OptixHandler::Impl::buildOptixContext(std::filesystem::path const &modulePath) {
    optixContext = std::make_unique<OptixContext>(*context, deviceContext, modulePath);
}

void OptixHandler::Impl::reset() noexcept {
    if (stream)
        cudaCheck<false>(cudaStreamSynchronize(stream));

    optixContext.reset();

    if (stream)
        cudaCheck<false>(cudaStreamDestroy(stream));
    if (deviceContext)
        optixCheck<false>(optixDeviceContextDestroy(deviceContext));

    stream = nullptr;
    deviceContext = nullptr;
    context.reset();
}

OptixHandler::OptixHandler(Ref<Context> context, std::filesystem::path const &modulePath)
    : impl_(std::make_unique<Impl>(std::move(context), modulePath)) {}

OptixHandler::~OptixHandler() = default;

void OptixHandler::launch() {
    auto const &sbt = impl_->optixContext->getSbt();
    // clang-format off
    optixCheck(optixLaunch(
        /* pipeline =           */ impl_->optixContext->getPipeline(),
        /* stream =             */ impl_->stream,
        /* pipelineParams =     */ 0,
        /* pipelineParamsSize = */ 0,
        /* sbt =                */ &sbt,
        /* width =              */ 1,
        /* height =             */ 1,
        /* depth =              */ 1));
    // clang-format on
    cudaCheck(cudaStreamSynchronize(impl_->stream));
}

Ref<Context> OptixHandler::getContext() const { return impl_->context; }
} // namespace flux
