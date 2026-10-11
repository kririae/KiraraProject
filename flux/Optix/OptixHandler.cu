#include <cuda.h>
#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "flux/Core/Logging.h"
#include "flux/Optix/DeletionQueue.h"
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
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/RenderProduct.h"
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

/// \brief Makes a CUDA context current on the calling thread for the scope's lifetime.
///
/// Leaving the scope makes current again exactly what the thread had current before, including no
/// context or a context the caller created.
class ContextScope final : private Noncopyable {
public:
    /// \brief Makes \p context current.
    /// \throw kira::Anyhow If the driver cannot push \p context.
    explicit ContextScope(CUcontext context) {
        cuCheck(cuCtxPushCurrent(context));
        pushed_ = true;
    }

    /// \brief Makes \p context current, or logs the failure and leaves the current context
    /// unchanged.
    ContextScope(CUcontext context, std::nothrow_t) noexcept {
        auto const result = cuCtxPushCurrent(context);
        cuCheck<false>(result);
        pushed_ = result == CUDA_SUCCESS;
    }

    ~ContextScope() {
        if (pushed_)
            cuCheck<false>(cuCtxPopCurrent(nullptr));
    }

private:
    /// Whether this scope pushed a context that it must still pop.
    bool pushed_{};
};

/// \brief The CUDA device, stream, and OptiX device context that one handler runs on.
///
/// Never changes the calling thread's current context. A resource that uses the stream must be
/// destroyed before the session, with \c getPrimaryContext() current.
class CudaDeviceSession final : private Noncopyable {
public:
    /// \brief Opens a session on CUDA device \p deviceOrdinal.
    /// \throw kira::Anyhow If \p deviceOrdinal is outside [0, device count) or CUDA fails.
    explicit CudaDeviceSession(int deviceOrdinal) : CudaDeviceSession(EmptyState{}) {
        cuCheck(cuInit(0));
        int deviceCount = 0;
        cuCheck(cuDeviceGetCount(&deviceCount));
        if (deviceCount == 0)
            throw kira::Anyhow("OptixHandler: no CUDA device is available");
        if (deviceOrdinal < 0 || deviceOrdinal >= deviceCount)
            throw kira::Anyhow(
                "OptixHandler: CUDA device {} is outside [0, {})", deviceOrdinal, deviceCount
            );

        CUdevice device{};
        cuCheck(cuDeviceGet(&device, deviceOrdinal));
        cuCheck(cuDevicePrimaryCtxRetain(&primaryContext_, device));
        device_ = device;

        ContextScope const scope(primaryContext_);
        cudaDeviceProp deviceProperties{};
        cudaCheck(cudaGetDeviceProperties(&deviceProperties, deviceOrdinal));
        LogInfo("OptixHandler: using CUDA device {} ('{}')", deviceOrdinal, deviceProperties.name);
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

    /// \brief Waits for the work queued on the stream, then closes the session.
    ~CudaDeviceSession() {
        if (!primaryContext_)
            return;

        {
            ContextScope const scope(primaryContext_, std::nothrow);
            if (stream_)
                cudaCheck<false>(cudaStreamSynchronize(stream_));
            if (context_)
                optixCheck<false>(optixDeviceContextDestroy(context_));
            if (stream_)
                cudaCheck<false>(cudaStreamDestroy(stream_));
        }
        cuCheck<false>(cuDevicePrimaryCtxRelease(device_));
    }

    /// \brief Returns the owned OptiX device context.
    [[nodiscard]] OptixDeviceContext get() const noexcept { return context_; }

    /// \brief Returns the stream used by every dependent resource.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return stream_; }

    /// \brief Returns the estimated number of megakernel threads the device keeps resident.
    [[nodiscard]] std::uint32_t getEstimatedResidentThreads() const noexcept {
        return estimatedResidentThreads_;
    }

    /// \brief Returns the retained primary context of the device.
    [[nodiscard]] CUcontext getPrimaryContext() const noexcept { return primaryContext_; }

private:
    struct EmptyState {};

    // Completing this target constructor makes the destructor responsible for
    // resources acquired before the public constructor body throws.
    explicit CudaDeviceSession(EmptyState) noexcept {}

    /// Null, or the primary context of \c device_ with one reference held by this session.
    CUcontext primaryContext_{};
    CUdevice device_{};

    /// Null, or a non-blocking stream in \c primaryContext_.
    cudaStream_t stream_{};

    /// Null, or an OptiX device context in \c primaryContext_ that reports through the FLux log.
    OptixDeviceContext context_{};
    std::uint32_t estimatedResidentThreads_{};
};

[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw kira::Anyhow("OptixHandler: context must not be null");
    return context;
}

inline constexpr std::uint64_t maxOptixLaunchDimension = std::uint64_t{1} << 30U;

struct ScaleFilmChannels {
    Film::Impl film;
    float factor;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept { film.scale(index, factor); }
};
} // namespace

struct OptixHandler::pImpl final {
    pImpl(Ref<Context> hostContext, std::filesystem::path const &modulePath, int deviceOrdinal);
    ~pImpl();

    /// \brief Updates the OptiX scene from the host \c Context.
    /// \pre The session's primary context is current.
    void sync();

    /// \brief Uploads \p params and launches a one-dimensional grid.
    /// \pre The session's primary context is current.
    void launch(OptixLaunchParams const &params, std::uint32_t size);

    /// \brief Returns the stream shared by this backend's device resources.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return deviceSession.getStream(); }

    /// \brief Returns how many threads to launch for \p chunkCount chunks.
    [[nodiscard]] std::uint32_t getLaunchSize(std::uint32_t chunkCount) const noexcept {
        auto const scaled =
            static_cast<double>(deviceSession.getEstimatedResidentThreads()) * launchWaves;
        auto const threads = static_cast<std::uint32_t>(std::lround(std::max(scaled, 1.0)));
        return std::min(chunkCount, std::max(threads, 1U));
    }

    /// Host \c Context retained for the lifetime of the backend.
    Ref<Context> context;

    /// CUDA execution state destroyed after every dependent resource.
    CudaDeviceSession deviceSession;

    /// Holds the session's context current while the members declared after it are constructed
    /// and destroyed, so their CUDA calls use that context. Empty whenever no \c pImpl
    /// constructor or destructor is running, so the current context never outlives a call.
    /// Destroyed before \c deviceSession releases the primary context.
    std::optional<ContextScope> memberScope;

    /// Deletes objects once the stream no longer uses them. Destroyed after every member that uses
    /// it.
    DeletionQueue deletionQueue{getStream()};

    std::filesystem::path modulePath;

    /// OptiX scene, or nothing after a failed update of it.
    std::unique_ptr<OptixContext> optixContext;

    /// Context version of the last successful sync, or nothing before the first.
    std::optional<std::uint64_t> syncedVersion;

    /// Active sampler, its sample offset, and the active integrator as of the last sync.
    Sampler::Impl sampler{};
    std::uint64_t sampleOffset{};
    PathIntegrator::Impl integrator;
    OptixRenderProductPool renderProducts{getStream()};
    DeviceBuffer<OptixLaunchParams> launchParams{getStream()};

    /// Next chunk not yet claimed by any lane. Zeroed before each launch.
    DeviceBuffer<std::uint32_t> chunkCounter{getStream()};
    CudaStreamTimer timer{getStream()};

    /// Threads to launch as a multiple of the estimated resident thread count.
    double launchWaves{2.0};
};

OptixHandler::pImpl::pImpl(
    Ref<Context> hostContext, std::filesystem::path const &modulePath, int deviceOrdinal
)
    : context(requireContext(std::move(hostContext))), deviceSession(deviceOrdinal),
      memberScope(std::in_place, deviceSession.getPrimaryContext()), modulePath(modulePath) {
    chunkCounter.resize(1);
    sync();
    memberScope.reset();
}

OptixHandler::pImpl::~pImpl() {
    // Member destruction enqueues stream releases that need the session's context current. C++
    // destroys the members declared after memberScope first, then memberScope, which pops, and
    // deviceSession last, which releases the primary context.
    memberScope.emplace(deviceSession.getPrimaryContext(), std::nothrow);
}

void OptixHandler::pImpl::sync() {
    // An equal version means an equal scene, so the device state and accumulation stand.
    auto const version = context->getVersion();
    if (optixContext && syncedVersion == version)
        return;

    // Render does not touch the Context, so read what it needs here. A failure here touches no
    // device state, so the OptiX scene stays.
    auto const activeSampler = context->getActiveSampler();
    auto const activeIntegrator = context->getActiveIntegrator();
    if (!activeIntegrator)
        throw kira::Anyhow("OptixHandler: context has no active integrator");
    if (!activeSampler)
        throw kira::Anyhow("OptixHandler: context has no active sampler");
    auto const nextSampler = activeSampler->getImpl();
    auto const nextSampleOffset = activeSampler->getSampleOffset();
    auto const nextIntegrator = activeIntegrator->getImpl();

    // A failure here leaves the device state unknown, so a later sync builds a new OptiX scene.
    try {
        deletionQueue.deleteFinished();

        if (!optixContext)
            optixContext.reset(
                new OptixContext(*context, deviceSession.get(), deletionQueue, modulePath)
            );
        optixContext->sync();
    } catch (...) {
        optixContext.reset();
        throw;
    }

    sampler = nextSampler;
    sampleOffset = nextSampleOffset;
    integrator = nextIntegrator;
    renderProducts.resetAccumulation();
    syncedVersion = version;
}

void OptixHandler::pImpl::launch(OptixLaunchParams const &params, std::uint32_t size) {
    launchParams.copyFromHost({&params, 1}, deletionQueue);
    optixContext->launch(
        getStream(), devicePointer(launchParams.data()), sizeof(OptixLaunchParams), size
    );
}

OptixHandler::OptixHandler(
    Ref<Context> context, std::filesystem::path const &modulePath, int deviceOrdinal
)
    : pImpl_(std::make_unique<pImpl>(std::move(context), modulePath, deviceOrdinal)) {}

OptixHandler::~OptixHandler() = default;

void OptixHandler::sync() {
    ContextScope const scope(pImpl_->deviceSession.getPrimaryContext());
    pImpl_->sync();
}

RenderStats OptixHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        return {};
    if (!pImpl_->optixContext)
        throw kira::Anyhow("OptixHandler: no scene, because the last sync failed");

    auto const &film = product.getFilm();
    auto const camera = product.getCamera().getImpl();

    // One path per pixel sample, with each pixel's samples consecutive. The launch is capped at
    // the resident thread count, so a lane whose chunk is done claims the next rather than
    // retiring and leaving its warp short.
    auto const pixelCount =
        static_cast<std::uint64_t>(film.getWidth()) * static_cast<std::uint64_t>(film.getHeight());
    if (pixelCount > maxOptixLaunchDimension || samples > maxOptixLaunchDimension / pixelCount)
        throw std::invalid_argument("OptixHandler: render batch exceeds the OptiX launch limit");
    auto const pathCount = static_cast<std::uint32_t>(pixelCount * samples);
    auto const &programSpec = pImpl_->optixContext->getProgramSpec();
    auto const chunkSize = std::min(programSpec.chunkSize, samples);
    auto const launchSize = pImpl_->getLaunchSize((pathCount - 1) / chunkSize + 1);

    ContextScope const scope(pImpl_->deviceSession.getPrimaryContext());
    pImpl_->deletionQueue.deleteFinished();

    // Film changes resize the product entry. A matching Camera::Impl keeps its
    // accumulation.
    auto &entry = pImpl_->renderProducts.getOrCreate(product);
    auto const accumulatedSamples = entry.accumulation && entry.accumulation->camera == camera
                                        ? entry.accumulation->samples
                                        : 0;

    try {
        // Clear accumulation before starting work. The next batch clears
        // partial pixels after a backend failure.
        entry.accumulation.reset();
        pImpl_->timer.start();
        auto const totalSamples = accumulatedSamples + samples;
        if (accumulatedSamples == 0) {
            entry.storage.forEach([](auto &channel) { channel.buffer.zero(); });
        } else if (entry.requestedChannels != FilmChannels::None) {
            auto const factor =
                static_cast<float>(accumulatedSamples) / static_cast<float>(totalSamples);
            launchLinearKernel(
                pixelCount, ScaleFilmChannels{.film = entry.film, .factor = factor},
                pImpl_->getStream()
            );
        }

        // The stream orders normalization, parameter upload, and OptiX work.
        pImpl_->chunkCounter.zero();
        auto const params = OptixLaunchParams{
            .scene = pImpl_->optixContext->getImpl(),
            .camera = camera,
            .sampler = pImpl_->sampler,
            .integrator = pImpl_->integrator,
            .bsdfDispatcher =
                {
                    .types = programSpec.bsdfTypes, // (2)
                },
            .accumulatedSamples = accumulatedSamples,
            .sampleOffset = pImpl_->sampleOffset,
            .nextChunk = pImpl_->chunkCounter.data(),
            .film = entry.film,
            .batchSize = samples,
            .pathCount = pathCount,
            .chunkSize = programSpec.chunkSize,         // (5)
            .shaderReorder = programSpec.shaderReorder, // (3)
            .hasEnvMap = programSpec.hasEnvMap,         // (4)
        };
        pImpl_->launch(params, launchSize);
        auto const elapsed = pImpl_->timer.stop();

        // Publish accumulation after all Film writes complete.
        entry.accumulation = OptixRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = totalSamples,
        };
        return {
            .paths = pathCount,
            .elapsed = elapsed,
        };
    } catch (...) {
        entry.accumulation.reset();
        throw;
    }
}

void OptixHandler::download(RenderProduct &product) {
    if (getAccumulatedSamples(product) == 0)
        throw kira::Anyhow("OptixHandler: render product has no valid accumulation");

    ContextScope const scope(pImpl_->deviceSession.getPrimaryContext());
    auto *entry = pImpl_->renderProducts.find(product);
    assert(entry);
    auto destination = product.getFilm().prepareDownload();
    entry->storage.forEach([&](auto &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        auto *output = destination.channels.template get<Channel>().data;
        channel.buffer.copyToHost({output, channel.buffer.size()}, pImpl_->getStream());
    });
}

void OptixHandler::setLaunchWaves(double waves) {
    if (!(waves > 0.0))
        throw kira::Anyhow("OptixHandler: launch waves must be positive, got {}", waves);
    pImpl_->launchWaves = waves;
}

std::uint32_t OptixHandler::getEstimatedResidentThreads() const {
    return pImpl_->deviceSession.getEstimatedResidentThreads();
}

std::uint64_t OptixHandler::getAccumulatedSamples(RenderProduct const &product) const {
    auto const *entry = pImpl_->renderProducts.find(product);
    auto const &film = product.getFilm();
    if (!entry || entry->film.width != film.getWidth() || entry->film.height != film.getHeight() ||
        entry->requestedChannels != film.getChannels() || !entry->accumulation)
        return 0;

    auto const camera = product.getCamera().getImpl();
    return entry->accumulation->camera == camera ? entry->accumulation->samples : 0;
}

bool OptixHandler::isConverged(RenderProduct const &product) const {
    return getAccumulatedSamples(product) >= product.getSamplesPerPixel();
}

void OptixHandler::release(RenderProduct const &product) noexcept {
    ContextScope const scope(pImpl_->deviceSession.getPrimaryContext(), std::nothrow);
    pImpl_->renderProducts.erase(product);
}

Ref<Context> OptixHandler::getContext() const { return pImpl_->context; }
} // namespace flux
