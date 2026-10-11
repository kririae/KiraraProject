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
#include "flux/Optix/OptixAccumulationBufferPImpl.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixDeviceSession.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/FilmImpl.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw std::invalid_argument("OptixHandler: context must not be null");
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
    // An equal version means an equal scene, so the device state stands.
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

RenderStats
OptixHandler::render(Camera const &camera, OptixAccumulationBuffer &buffer, std::uint32_t samples) {
    if (samples == 0)
        return {};
    if (!pImpl_->optixContext)
        throw std::invalid_argument("OptixHandler: no scene, because the last sync failed");

    // The launch runs on this handler's stream, and the buffer's storage is ordered on the stream
    // of the handler that built it. Two streams would race on the pixels.
    auto &impl = *buffer.pImpl_;
    if (impl.session != &pImpl_->deviceSession)
        throw std::invalid_argument("OptixHandler: buffer was created by another handler");

    auto const &film = impl.film;
    auto const cameraImpl = camera.getImpl();

    // One path per pixel sample, with each pixel's samples consecutive. The launch is capped at
    // the resident thread count, so a lane whose chunk is done claims the next rather than
    // retiring and leaving its warp short.
    auto const pixelCount =
        static_cast<std::uint64_t>(film.width) * static_cast<std::uint64_t>(film.height);
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

    auto const accumulatedSamples = impl.samples;
    try {
        // Discard the samples before starting work. The next render zeroes partial pixels after a
        // backend failure.
        impl.samples = 0;
        pImpl_->timer.start();
        auto const totalSamples = accumulatedSamples + samples;
        if (accumulatedSamples == 0) {
            impl.storage.forEach([](auto &channel) { channel.buffer.zero(); });
        } else if (impl.getChannels() != FilmChannels::None) {
            auto const factor =
                static_cast<float>(accumulatedSamples) / static_cast<float>(totalSamples);
            launchLinearKernel(
                pixelCount, ScaleFilmChannels{.film = film, .factor = factor}, pImpl_->getStream()
            );
        }

        // The stream orders normalization, parameter upload, and OptiX work.
        pImpl_->chunkCounter.zero();
        auto const params = OptixLaunchParams{
            .scene = pImpl_->optixContext->getImpl(),
            .camera = cameraImpl,
            .sampler = pImpl_->sampler,
            .integrator = pImpl_->integrator,
            .bsdfDispatcher =
                {
                    .types = programSpec.bsdfTypes, // (2)
                },
            .accumulatedSamples = accumulatedSamples,
            .sampleOffset = pImpl_->sampleOffset,
            .nextChunk = pImpl_->chunkCounter.data(),
            .film = film,
            .batchSize = samples,
            .pathCount = pathCount,
            .chunkSize = programSpec.chunkSize, // (5)
            .chunkCount = chunkCount,
            .shaderReorder = programSpec.shaderReorder, // (3)
            .hasEnvMap = programSpec.hasEnvMap,         // (4)
        };
        pImpl_->launch(params, launchSize);
        auto const elapsed = pImpl_->timer.stop();

        // Count the samples after all Film writes complete.
        impl.samples = totalSamples;
        return {
            .paths = pathCount,
            .elapsed = elapsed,
        };
    } catch (...) {
        impl.samples = 0;
        throw;
    }
}

void OptixHandler::setLaunchWaves(double waves) {
    if (!(waves > 0.0))
        throw kira::Anyhow("OptixHandler: launch waves must be positive, got {}", waves);
    pImpl_->launchWaves = waves;
}

std::uint32_t OptixHandler::getEstimatedResidentThreads() const {
    return pImpl_->deviceSession.getEstimatedResidentThreads();
}

Ref<Context> OptixHandler::getContext() const { return pImpl_->context; }

OptixDeviceSession const &OptixHandler::getDeviceSession() const noexcept {
    return pImpl_->deviceSession;
}
} // namespace flux
