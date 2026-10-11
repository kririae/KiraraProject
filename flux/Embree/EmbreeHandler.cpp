#include "flux/Embree/EmbreeHandler.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "flux/Embree/EmbreeContext.h"
#include "flux/Embree/EmbreeLaunchParams.h"
#include "flux/Embree/EmbreeMegaKernel.h"
#include "flux/Embree/EmbreeRenderProductPool.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw kira::Anyhow("EmbreeHandler: context must not be null");
    return context;
}
} // namespace

struct EmbreeHandler::pImpl {
    explicit pImpl(Ref<Context> hostContext)
        : context(requireContext(std::move(hostContext))), embreeContext(*context) {
        sync();
    }

    void sync() {
        // An equal version means an equal scene, so the scene and accumulation stand.
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

        renderProducts.resetAccumulation();
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
    EmbreeRenderProductPool renderProducts;
};

EmbreeHandler::EmbreeHandler(Ref<Context> context)
    : pImpl_(std::make_unique<pImpl>(std::move(context))) {}

EmbreeHandler::~EmbreeHandler() = default;

void EmbreeHandler::sync() { pImpl_->sync(); }

RenderStats EmbreeHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        return {};

    auto const &film = product.getFilm();
    if (film.getHeight() > std::numeric_limits<std::size_t>::max() / film.getWidth())
        throw std::invalid_argument("EmbreeHandler: film dimensions are too large");
    auto const pixelCount =
        static_cast<std::size_t>(film.getWidth()) * static_cast<std::size_t>(film.getHeight());
    auto const batchPaths = static_cast<std::uint64_t>(pixelCount) * samples;
    auto const camera = product.getCamera().getImpl();

    // Film changes resize the product entry. A matching Camera::Impl keeps its
    // accumulation.
    auto &entry = pImpl_->renderProducts.getOrCreate(product);
    auto const accumulatedSamples = entry.accumulation && entry.accumulation->camera == camera
                                        ? entry.accumulation->samples
                                        : 0;

    auto const params = EmbreeLaunchParams{
        .scene = pImpl_->embreeContext.getImpl(),
        .camera = camera,
        .sampler = pImpl_->sampler,
        .integrator = pImpl_->integrator,
        .accumulatedSamples = accumulatedSamples,
        .sampleOffset = pImpl_->sampleOffset,
        .film = entry.film,
        .batchSize = samples,
    };

    try {
        // Clear accumulation before starting work. The next batch clears
        // partial pixels after a backend failure.
        entry.accumulation.reset();
        auto const start = std::chrono::steady_clock::now();
        std::uint64_t renderedPaths = 0;
        if (entry.requestedChannels != FilmChannels::None) {
            tbb::parallel_for(
                tbb::blocked_range<std::size_t>{0, pixelCount},
                [&](tbb::blocked_range<std::size_t> const &range) {
                auto const scope = embree::LaunchParamsScope{params};
                for (auto index = range.begin(); index != range.end(); ++index)
                    embree::runMegaKernel(params, index);
            }
            );
            renderedPaths = batchPaths;
        }
        auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start
        );

        // Publish accumulation after all Film writes complete.
        entry.accumulation = EmbreeRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = accumulatedSamples + samples,
        };
        return {
            .paths = renderedPaths,
            .elapsed = elapsed,
        };
    } catch (...) {
        entry.accumulation.reset();
        throw;
    }
}

void EmbreeHandler::download(RenderProduct &product) {
    if (getAccumulatedSamples(product) == 0)
        throw kira::Anyhow("EmbreeHandler: render product has no valid accumulation");

    auto *entry = pImpl_->renderProducts.find(product);
    assert(entry);
    auto destination = product.getFilm().getImpl();
    entry->storage.forEach([&](auto const &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        if (!channel.values.empty()) {
            auto *output = destination.channels.template get<Channel>().data;
            assert(output);
            std::ranges::copy(channel.values, output);
        }
    });
}

std::uint64_t EmbreeHandler::getAccumulatedSamples(RenderProduct const &product) const {
    auto const *entry = pImpl_->renderProducts.find(product);
    if (!entry || !entry->accumulation)
        return 0;
    auto const &film = product.getFilm();
    if (entry->film.width != film.getWidth() || entry->film.height != film.getHeight() ||
        entry->requestedChannels != film.getChannels())
        return 0;
    auto const camera = product.getCamera().getImpl();
    return entry->accumulation->camera == camera ? entry->accumulation->samples : 0;
}

bool EmbreeHandler::isConverged(RenderProduct const &product) const {
    return getAccumulatedSamples(product) >= product.getSamplesPerPixel();
}

void EmbreeHandler::release(RenderProduct const &product) noexcept {
    pImpl_->renderProducts.erase(product);
}

Ref<Context> EmbreeHandler::getContext() const { return pImpl_->context; }
} // namespace flux
