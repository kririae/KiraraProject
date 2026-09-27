#include "flux/Embree/EmbreeHandler.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
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

struct EmbreeHandler::Impl {
    explicit Impl(Ref<Context> hostContext)
        : context(requireContext(std::move(hostContext))), embreeContext(*context) {
        sync();
    }

    void sync() {
        (void)context->getActiveIntegrator();
        (void)context->getActiveSampler();
        renderProducts.resetAccumulation();
        embreeContext.sync();
    }

    Ref<Context> context;
    EmbreeContext embreeContext;
    EmbreeRenderProductPool renderProducts;
    std::uint64_t sampleOffset{};
};

EmbreeHandler::EmbreeHandler(Ref<Context> context)
    : impl_(std::make_unique<Impl>(std::move(context))) {}

EmbreeHandler::~EmbreeHandler() = default;

void EmbreeHandler::sync() { impl_->sync(); }

RenderStats EmbreeHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        throw std::invalid_argument("EmbreeHandler: sample batch must be nonzero");

    auto const &film = product.getFilm();
    if (film.getHeight() > std::numeric_limits<std::size_t>::max() / film.getWidth())
        throw std::invalid_argument("EmbreeHandler: film dimensions are too large");
    auto const pixelCount =
        static_cast<std::size_t>(film.getWidth()) * static_cast<std::size_t>(film.getHeight());
    if (pixelCount != 0 && samples > std::numeric_limits<std::uint64_t>::max() / pixelCount)
        throw std::invalid_argument("EmbreeHandler: render batch path count overflows");
    auto const batchPaths = static_cast<std::uint64_t>(pixelCount) * samples;
    auto const sampler = impl_->context->getActiveSampler();
    auto const integrator = impl_->context->getActiveIntegrator();
    auto const camera = product.getCamera().getImpl();

    // Film changes resize the product entry. A matching Camera::Impl keeps its
    // accumulation.
    auto &entry = impl_->renderProducts.getOrCreate(product);
    auto const accumulatedSamples = entry.accumulation && entry.accumulation->camera == camera
                                        ? entry.accumulation->samples
                                        : 0;
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - samples)
        throw std::invalid_argument("EmbreeHandler: accumulated sample count overflows");
    auto const lastBatchIndex = static_cast<std::uint64_t>(samples - 1);
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - impl_->sampleOffset ||
        accumulatedSamples + impl_->sampleOffset >
            std::numeric_limits<std::uint64_t>::max() - lastBatchIndex)
        throw std::invalid_argument("EmbreeHandler: sample sequence index overflows");

    auto const params = EmbreeLaunchParams{
        .scene = impl_->embreeContext.getImpl(),
        .camera = camera,
        .sampler = sampler->getImpl({film.getWidth(), film.getHeight()}),
        .integrator = integrator->getImpl(),
        .accumulatedSamples = accumulatedSamples,
        .sampleOffset = impl_->sampleOffset,
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

    auto *entry = impl_->renderProducts.find(product);
    assert(entry);
    auto destination = product.getFilm().prepareDownload();
    entry->storage.forEach([&](auto const &channel) {
        using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
        if (!channel.values.empty()) {
            auto *output = destination.channels.template get<Channel>().data;
            assert(output);
            std::ranges::copy(channel.values, output);
        }
    });
}

void EmbreeHandler::setSampleOffset(std::uint64_t offset) {
    if (impl_->sampleOffset == offset)
        return;
    impl_->sampleOffset = offset;
    impl_->renderProducts.resetAccumulation();
}

std::uint64_t EmbreeHandler::getAccumulatedSamples(RenderProduct const &product) const {
    auto const *entry = impl_->renderProducts.find(product);
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
    impl_->renderProducts.erase(product);
}

Ref<Context> EmbreeHandler::getContext() const { return impl_->context; }
} // namespace flux
