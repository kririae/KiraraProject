#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixRenderProductPool.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
inline constexpr std::uint64_t maxOptixLaunchDimension = std::uint64_t{1} << 30U;

struct ScaleNormalChannel {
    Vec3f *normal;
    float factor;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept {
        normal[index] = normal[index] * factor;
    }
};
} // namespace

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

    /// \brief Uploads \p params and launches a one-dimensional grid.
    void launch(OptixLaunchParams const &params, std::uint32_t size);

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
    std::uint64_t sampleOffset{};
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

void OptixHandler::Impl::sync() {
    optixContext->sync();
    renderProducts->resetAccumulation();
}

void OptixHandler::Impl::launch(OptixLaunchParams const &params, std::uint32_t size) {
    launchParams.copyFromHost({&params, 1}, stream);
    optixContext->launch(
        stream, devicePointer(launchParams.data()), sizeof(OptixLaunchParams), size
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

void OptixHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        throw std::invalid_argument("OptixHandler: sample batch must be nonzero");

    // Resolve launch-time inputs. None of these objects belong to the
    // persistent OptiX scene.
    auto const &film = product.getFilm();
    auto const resolution = Vec2u{film.getWidth(), film.getHeight()};
    auto const sampler = impl_->context->getActiveSampler();
    auto const camera = product.getCamera().getDeviceImpl();

    // Bound values are baked into the module. A changed host spec needs a new
    // module before its launch data can be used.
    if (impl_->optixContext->getProgramSpec() != OptixProgram::makeSpec(*impl_->context))
        throw kira::Anyhow(
            "OptixHandler: program specialization changed; call sync before rendering"
        );

    // OptiX launches one work item per pixel sample. Samples of the same pixel
    // remain consecutive in the one-dimensional launch.
    auto const pixelCount =
        static_cast<std::uint64_t>(film.getWidth()) * static_cast<std::uint64_t>(film.getHeight());
    if (pixelCount > maxOptixLaunchDimension || samples > maxOptixLaunchDimension / pixelCount)
        throw std::invalid_argument("OptixHandler: render batch exceeds the OptiX launch limit");
    auto const launchSize = static_cast<std::uint32_t>(pixelCount * samples);

    // Film changes update this product's existing entry. Camera compatibility
    // only controls whether its pixel history can be reused.
    auto &target = impl_->renderProducts->update(product);
    auto const accumulatedSamples = target.accumulation && target.accumulation->camera == camera
                                        ? target.accumulation->samples
                                        : 0;
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - samples)
        throw std::invalid_argument("OptixHandler: accumulated sample count overflows");
    auto const lastBatchIndex = static_cast<std::uint64_t>(samples - 1);
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - impl_->sampleOffset ||
        accumulatedSamples + impl_->sampleOffset >
            std::numeric_limits<std::uint64_t>::max() - lastBatchIndex)
        throw std::invalid_argument("OptixHandler: sample sequence index overflows");

    try {
        // Pixel writes below may leave a partial image if CUDA reports an
        // asynchronous failure. Clear the validity marker before enqueueing
        // them so the next batch starts from a zeroed target.
        target.accumulation.reset();
        auto const totalSamples = accumulatedSamples + samples;
        if (accumulatedSamples == 0) {
            target.normal.zero();
        } else {
            auto const factor =
                static_cast<float>(accumulatedSamples) / static_cast<float>(totalSamples);
            launchLinearKernel(
                target.normal.size(),
                ScaleNormalChannel{.normal = target.normal.data(), .factor = factor}, impl_->stream
            );
        }

        // The stream orders normalization, parameter upload, and OptiX work.
        // Synchronization below also closes the host lifetime of launch data.
        auto const params = OptixLaunchParams{
            .scene = impl_->optixContext->getDeviceImpl(),
            .camera = camera,
            .sampler = sampler->getDeviceImpl(resolution),
            .accumulatedSamples = accumulatedSamples,
            .sampleOffset = impl_->sampleOffset,
            .film = target.film,
            .batchSize = samples,
        };
        impl_->launch(params, launchSize);
        cudaCheck(cudaStreamSynchronize(impl_->stream));

        // Publish the new history only after all device writes have completed.
        target.accumulation = OptixRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = totalSamples,
        };
    } catch (...) {
        target.accumulation.reset();
        cudaCheck<false>(cudaStreamSynchronize(impl_->stream));
        throw;
    }
}

void OptixHandler::setSampleOffset(std::uint64_t offset) {
    if (impl_->sampleOffset == offset)
        return;
    impl_->sampleOffset = offset;
    impl_->renderProducts->resetAccumulation();
}

std::uint64_t OptixHandler::getAccumulatedSamples(RenderProduct const &product) const {
    auto const *target = impl_->renderProducts->find(product);
    auto const &film = product.getFilm();
    if (!target || target->film.width != film.getWidth() ||
        target->film.height != film.getHeight() || !target->accumulation)
        return 0;

    auto const camera = product.getCamera().getDeviceImpl();
    return target->accumulation->camera == camera ? target->accumulation->samples : 0;
}

void OptixHandler::release(RenderProduct const &product) noexcept {
    impl_->renderProducts->erase(product);
}

Ref<Context> OptixHandler::getContext() const { return impl_->context; }
} // namespace flux
