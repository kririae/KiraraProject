#include "flux/Optix/OptixHandler.h"

#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixRenderProductPool.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"
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

    /// \brief Uploads \p params and launches a two-dimensional grid.
    void launch(OptixLaunchParams const &params, std::uint32_t width, std::uint32_t height);

    /// \brief Releases backend resources in dependency order without throwing.
    void reset() noexcept;

    /// Host scene retained for the lifetime of the backend.
    Ref<Context> context;

    OptixDeviceContext deviceContext{};
    cudaStream_t stream{};

    /// Device scene destroyed before the stream and device context.
    std::unique_ptr<OptixContext> optixContext;

    std::optional<OptixRenderProductPool> renderProducts;
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
    renderProducts.emplace(stream);
}

void OptixHandler::Impl::buildOptixContext(std::filesystem::path const &modulePath) {
    optixContext.reset(new OptixContext(*context, deviceContext, stream, modulePath));
}

void OptixHandler::Impl::sync() { optixContext->sync(); }

void OptixHandler::Impl::launch(
    OptixLaunchParams const &params, std::uint32_t width, std::uint32_t height
) {
    launchParams.copyFromHost({&params, 1}, stream);
    optixContext->launch(
        stream, devicePointer(launchParams.data()), sizeof(OptixLaunchParams), width, height
    );
}

void OptixHandler::Impl::reset() noexcept {
    if (stream)
        cudaCheck<false>(cudaStreamSynchronize(stream));

    launchParams.clear(stream);
    renderProducts.reset();
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

void OptixHandler::render(Camera const &camera, RenderProduct const &product) {
    if (camera.getContext() != impl_->context.get() || product.getContext() != impl_->context.get())
        throw std::invalid_argument(
            "OptixHandler: camera and render product must belong to the handler context"
        );

    auto const &film = product.getFilm();
    auto const params = OptixLaunchParams{
        .scene = impl_->optixContext->getDeviceImpl(),
        .camera = camera.getDeviceImpl(),
        .film = impl_->renderProducts->acquire(product),
    };
    try {
        impl_->launch(params, film.getWidth(), film.getHeight());
        cudaCheck(cudaStreamSynchronize(impl_->stream));
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(impl_->stream));
        throw;
    }
}

Ref<Context> OptixHandler::getContext() const { return impl_->context; }
} // namespace flux
