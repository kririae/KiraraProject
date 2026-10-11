#include <cuda.h>
#include <cuda_runtime_api.h>
#include <optix_stubs.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixDeviceSession.h"
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
[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw kira::Anyhow("OptixHandler: context must not be null");
    return context;
}

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
    OptixDeviceSession deviceSession;

    /// Holds the session's context current while the members declared after it are constructed
    /// and destroyed, so their CUDA calls use that context. Empty whenever no \c pImpl
    /// constructor or destructor is running, so the current context never outlives a call.
    /// Destroyed before \c deviceSession releases the primary context.
    std::optional<CudaContextGuard> cudaContextGuard;

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
      cudaContextGuard(std::in_place, deviceSession.getPrimaryContext()), modulePath(modulePath) {
    chunkCounter.resize(1);
    sync();
    cudaContextGuard.reset();
}

OptixHandler::pImpl::~pImpl() {
    // Member destruction enqueues stream releases that need the session's context current. C++
    // destroys the members declared after cudaContextGuard first, then cudaContextGuard, which
    // pops, and deviceSession last, which releases the primary context.
    cudaContextGuard.emplace(deviceSession.getPrimaryContext(), std::nothrow);
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
    CudaContextGuard const guard(pImpl_->deviceSession.getPrimaryContext());
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
    constexpr auto maxPaths = std::numeric_limits<decltype(OptixLaunchParams::pathCount)>::max();
    if (pixelCount > maxPaths / samples)
        throw std::length_error("OptixHandler: render has more paths than the path index counts");
    auto const pathCount = static_cast<std::uint32_t>(pixelCount * samples);
    auto const &programSpec = pImpl_->optixContext->getProgramSpec();
    auto const chunkSize = std::min(programSpec.chunkSize, samples);
    auto const chunkCount = (pathCount - 1) / chunkSize + 1;
    auto const launchSize = pImpl_->getLaunchSize(chunkCount);

    CudaContextGuard const guard(pImpl_->deviceSession.getPrimaryContext());
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
            .chunkSize = programSpec.chunkSize, // (5)
            .chunkCount = chunkCount,
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

    CudaContextGuard const guard(pImpl_->deviceSession.getPrimaryContext());
    auto *entry = pImpl_->renderProducts.find(product);
    assert(entry);
    auto destination = product.getFilm().getImpl();
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
    CudaContextGuard const guard(pImpl_->deviceSession.getPrimaryContext(), std::nothrow);
    pImpl_->renderProducts.erase(product);
}

Ref<Context> OptixHandler::getContext() const { return pImpl_->context; }
} // namespace flux
