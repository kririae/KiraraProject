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

        // Render does not touch the Context, so read what it needs here.
        auto const activeSampler = context->getActiveSampler();
        auto const activeIntegrator = context->getActiveIntegrator();
        if (!activeIntegrator)
            throw kira::Anyhow("EmbreeHandler: context has no active integrator");
        if (!activeSampler)
            throw kira::Anyhow("EmbreeHandler: context has no active sampler");
        sampler = activeSampler->getImpl();
        sampleOffset = activeSampler->getSampleOffset();
        integrator = activeIntegrator->getImpl();

        embreeContext.sync();
        syncedVersion = version;
    }

    /// \brief Renders \p samples samples per pixel from \p camera into \p buffer.
    /// \pre \p samples is nonzero.
    RenderStats
    render(Camera const &camera, EmbreeAccumulationBuffer::pImpl &buffer, std::uint32_t samples);

    Ref<Context> context;

    /// The Embree scene. After a failed sync it is valid only for destruction.
    EmbreeContext embreeContext;

    /// Context version of the last successful sync, or nothing before the first.
    std::optional<std::uint64_t> syncedVersion;

    /// Active sampler, its sample offset, and the active integrator as of the last sync.
    Sampler::Impl sampler{};
    std::uint64_t sampleOffset{};
    PathIntegrator::Impl integrator;

    /// Whether an exception has escaped \c sync or \c render. It is set once and never cleared.
    /// The scene may then be partly updated, so the handler does no later work.
    bool failed{};
};

EmbreeHandler::EmbreeHandler(Ref<Context> context)
    : pImpl_(std::make_unique<pImpl>(std::move(context))) {}

EmbreeHandler::~EmbreeHandler() = default;

void EmbreeHandler::sync() {
    if (pImpl_->failed)
        return;
    try {
        pImpl_->sync();
    } catch (...) {
        pImpl_->failed = true;
        throw;
    }
}

RenderStats EmbreeHandler::pImpl::render(
    Camera const &camera, EmbreeAccumulationBuffer::pImpl &buffer, std::uint32_t samples
) {
    auto const pixelCount = static_cast<std::uint64_t>(buffer.film.width) *
                            static_cast<std::uint64_t>(buffer.film.height);
    auto const accumulatedSamples = buffer.samples;

    auto const params = EmbreeLaunchParams{
        .scene = embreeContext.getImpl(),
        .camera = camera.getImpl(),
        .sampler = sampler,
        .integrator = integrator,
        .accumulatedSamples = accumulatedSamples,
        .sampleOffset = sampleOffset,
        .film = buffer.film,
        .batchSize = samples,
    };

    try {
        // Discard the samples before starting work. The next render zeroes partial pixels after a
        // failure.
        buffer.samples = 0;
        auto const start = std::chrono::steady_clock::now();
        std::uint64_t renderedPaths = 0;
        if (accumulatedSamples == 0)
            buffer.storage.forEach([](auto &channel) {
                std::ranges::fill(
                    channel.values, std::ranges::range_value_t<decltype(channel.values)>{}
                );
            });
        if (buffer.getChannels() != FilmChannels::None) {
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
        buffer.samples = accumulatedSamples + samples;
        return {
            .paths = renderedPaths,
            .elapsed = elapsed,
        };
    } catch (...) {
        buffer.samples = 0;
        throw;
    }
}

RenderStats EmbreeHandler::render(
    Camera const &camera, EmbreeAccumulationBuffer &buffer, std::uint32_t samples
) {
    if (pImpl_->failed || samples == 0)
        return {};
    try {
        return pImpl_->render(camera, *buffer.pImpl_, samples);
    } catch (...) {
        pImpl_->failed = true;
        throw;
    }
}

bool EmbreeHandler::hasFailed() const noexcept { return pImpl_->failed; }

Ref<Context> EmbreeHandler::getContext() const { return pImpl_->context; }
} // namespace flux
