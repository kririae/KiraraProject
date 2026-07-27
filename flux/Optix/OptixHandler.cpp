#include "flux/Optix/OptixHandler.h"

#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdint>
#include <limits>
#include <utility>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixLaunchParams.h"
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

    /// \brief Rebuilds the device scene from the host context.
    void sync();

    /// \brief Uploads \p params and launches \p width work items.
    void launch(OptixLaunchParams const &params, std::uint32_t width);

    /// \brief Releases backend resources in dependency order without throwing.
    void reset() noexcept;

    /// Host scene retained for the lifetime of the backend.
    Ref<Context> context;

    OptixDeviceContext deviceContext{};
    cudaStream_t stream{};

    /// Device scene destroyed before the stream and device context.
    std::unique_ptr<OptixContext> optixContext;

    DeviceBuffer<Ray> rays;
    DeviceBuffer<RayHit> hits;
    DeviceBuffer<OptixLaunchParams> launchParams;
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
        sync();
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
    optixContext.reset(new OptixContext(deviceContext, stream, modulePath));
}

void OptixHandler::Impl::sync() { optixContext->sync(*context); }

void OptixHandler::Impl::launch(OptixLaunchParams const &params, std::uint32_t width) {
    launchParams.copyFromHost({&params, 1}, stream);
    optixContext->launch(
        stream, devicePointer(launchParams.data()), sizeof(OptixLaunchParams), width
    );
}

void OptixHandler::Impl::reset() noexcept {
    if (stream)
        cudaCheck<false>(cudaStreamSynchronize(stream));

    launchParams.clear(stream);
    hits.clear(stream);
    rays.clear(stream);
    optixContext.reset();

    if (stream)
        cudaCheck<false>(cudaStreamSynchronize(stream));
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

void OptixHandler::sync() { impl_->sync(); }

void OptixHandler::launch() {
    auto const params = OptixLaunchParams{
        .traversable = impl_->optixContext->getTraversable(),
    };
    try {
        impl_->launch(params, 1);
        cudaCheck(cudaStreamSynchronize(impl_->stream));
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(impl_->stream));
        throw;
    }
}

std::vector<RayHit> OptixHandler::intersect(std::span<Ray const> rays) {
    if (rays.size() > std::numeric_limits<std::uint32_t>::max())
        throw kira::Anyhow("OptixHandler: ray count exceeds OptiX launch limits");
    if (rays.empty())
        return {};

    try {
        impl_->rays.copyFromHost({rays.data(), rays.size()}, impl_->stream);
        impl_->hits.resize(rays.size(), impl_->stream);

        auto const params = OptixLaunchParams{
            .traversable = impl_->optixContext->getTraversable(),
            .rays = impl_->rays.data(),
            .hits = impl_->hits.data(),
            .rayCount = static_cast<std::uint32_t>(rays.size()),
        };
        impl_->launch(params, params.rayCount);

        std::vector<RayHit> result(rays.size());
        impl_->hits.copyToHost({result.data(), result.size()}, impl_->stream);
        cudaCheck(cudaStreamSynchronize(impl_->stream));
        return result;
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(impl_->stream));
        throw;
    }
}

Ref<Context> OptixHandler::getContext() const { return impl_->context; }
} // namespace flux
