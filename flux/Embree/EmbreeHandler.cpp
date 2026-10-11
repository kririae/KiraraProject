#include "flux/Embree/EmbreeHandler.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "flux/Embree/EmbreeAccumulationBufferPImpl.h"
#include "flux/Embree/EmbreeContext.h"
#include "flux/Embree/EmbreeLaunchParams.h"
#include "flux/Embree/EmbreeMegaKernel.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw std::invalid_argument("EmbreeHandler: context must not be null");
    return context;
}
} // namespace

struct EmbreeHandler::pImpl {
    explicit pImpl(Ref<Context> hostContext)
        : context(requireContext(std::move(hostContext))), embreeContext(*context) {
        sync();
    }

    void sync() {
        // An equal version means an equal scene, so the Embree scene stands.
        auto const version = context->getVersion();
        if (syncedVersion == version)
            return;

        // Read what render needs before the device state changes, since render does not touch
        // the Context.
        auto const activeSampler = context->getActiveSampler();
        auto const activeIntegrator = context->getActiveIntegrator();
        if (!activeIntegrator)
            throw kira::Anyhow("EmbreeHandler: context has no active integrator");
        if (!activeSampler)
            throw kira::Anyhow("EmbreeHandler: context has no active sampler");
        auto const nextSampler = activeSampler->getImpl();
        auto const nextSampleOffset = activeSampler->getSampleOffset();
        auto const nextIntegrator = activeIntegrator->getImpl();

        syncedVersion.reset();
        embreeContext.sync();
        sampler = nextSampler;
        sampleOffset = nextSampleOffset;
        integrator = nextIntegrator;
        syncedVersion = version;
    }

    Ref<Context> context;
    EmbreeContext embreeContext;

    /// Context version of the last successful sync, or nothing before the first and after a
    /// failed rebuild of the Embree scene.
    std::optional<std::uint64_t> syncedVersion;

    /// Active sampler, its sample offset, and the active integrator as of the last sync.
    Sampler::Impl sampler{};
    std::uint64_t sampleOffset{};
    PathIntegrator::Impl integrator;
};

EmbreeHandler::EmbreeHandler(Ref<Context> context)
    : pImpl_(std::make_unique<pImpl>(std::move(context))) {}

EmbreeHandler::~EmbreeHandler() = default;

void EmbreeHandler::sync() { pImpl_->sync(); }

RenderStats EmbreeHandler::render(
    Camera const &camera, EmbreeAccumulationBuffer &buffer, std::uint32_t samples
) {
    if (samples == 0)
        return {};
    if (!pImpl_->syncedVersion)
        throw std::invalid_argument("EmbreeHandler: no scene, because the last sync failed");

    auto &impl = *buffer.pImpl_;
    auto const pixelCount =
        static_cast<std::uint64_t>(impl.film.width) * static_cast<std::uint64_t>(impl.film.height);
    auto const accumulatedSamples = impl.samples;

    auto const params = EmbreeLaunchParams{
        .scene = pImpl_->embreeContext.getImpl(),
        .camera = camera.getImpl(),
        .sampler = pImpl_->sampler,
        .integrator = pImpl_->integrator,
        .accumulatedSamples = accumulatedSamples,
        .sampleOffset = pImpl_->sampleOffset,
        .film = impl.film,
        .batchSize = samples,
    };

    try {
        // Discard the samples before starting work. The next render zeroes partial pixels after a
        // failure.
        impl.samples = 0;
        auto const start = std::chrono::steady_clock::now();
        std::uint64_t renderedPaths = 0;
        if (accumulatedSamples == 0)
            impl.storage.forEach([](auto &channel) {
                std::ranges::fill(
                    channel.values, std::ranges::range_value_t<decltype(channel.values)>{}
                );
            });
        if (impl.getChannels() != FilmChannels::None) {
            tbb::parallel_for(
                tbb::blocked_range<std::size_t>{0, pixelCount},
                [&](tbb::blocked_range<std::size_t> const &range) {
                auto const scope = embree::LaunchParamsScope{params};
                for (auto index = range.begin(); index != range.end(); ++index)
                    embree::runMegaKernel(params, index);
            }
            );
            renderedPaths = pixelCount * samples;
        }
        auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start
        );

        // Count the samples after all Film writes complete.
        impl.samples = accumulatedSamples + samples;
        return {
            .paths = renderedPaths,
            .elapsed = elapsed,
        };
    } catch (...) {
        impl.samples = 0;
        throw;
    }
}

Ref<Context> EmbreeHandler::getContext() const { return pImpl_->context; }
} // namespace flux
