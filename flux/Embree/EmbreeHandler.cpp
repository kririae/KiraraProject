#include "flux/Embree/EmbreeHandler.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include "flux/Embree/EmbreeContext.h"
#include "flux/Embree/EmbreeLaunchParams.h"
#include "flux/Embree/EmbreeRenderProductPool.h"
#include "flux/Integrator/PathIntegratorImpl.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/DiffuseBSDFImpl.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
thread_local EmbreeLaunchParams const *currentLaunchParams;

[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw kira::Anyhow("EmbreeHandler: context must not be null");
    return context;
}

void renderPixel(std::size_t pixelIndex) noexcept {
    auto const &params = embree::detail::getLaunchParams();
    auto const resolution = Vec2u{params.film.width, params.film.height};
    auto const pixel = Vec2u{
        static_cast<std::uint32_t>(pixelIndex % params.film.width),
        static_cast<std::uint32_t>(pixelIndex / params.film.width),
    };
    Vec3f normalSum{};
    Vec3f albedoSum{};

    for (std::uint32_t batchIndex = 0; batchIndex < params.batchSize; ++batchIndex) {
        auto sampler = params.sampler;
        sampler.startPixelSample(pixel, params.getSampleIndex(batchIndex), resolution);
        auto const pixelSample = sampler.getPixel2D();
        auto const rasterPosition = Vec2f{
            static_cast<float>(pixel.x()) + pixelSample.x(),
            static_cast<float>(pixel.y()) + pixelSample.y(),
        };
        auto const ray = params.camera.generateRay(rasterPosition, sampler.get2D(), resolution);
        PathState state{
            .ray = ray,
            .sampler = sampler,
        };

        // Embree schedules each path. Every integrator call advances the same
        // PathState used by the OptiX megakernel.
        while (state.active) {
            EmbreeContext::Hit hit;
            if (!params.scene->intersect(state.ray, hit)) {
                PathIntegrator::Impl{}.onMiss(state);
                continue;
            }

            auto surface = params.scene->makeSurfaceInteraction(state.ray, hit);
            auto const &primitive = params.scene->getPrimitive(hit.primitiveIndex);
            if (primitive.hasBSDF()) {
                auto bsdf = params.scene->getBSDF(primitive.getBSDFIndex());
                auto const wo = -state.ray.direction;
                bsdf.init(surface, wo);
                if (state.bounce == 0 && params.film.hasChannel<AlbedoChannel>()) {
                    auto const query = BSDFQuery{.surface = surface, .wo = wo};
                    auto const sample =
                        bsdf.sample(query, state.sampler.get1D(), state.sampler.get2D());
                    if (sample.pdf > 0.0F) {
                        auto const cosine = std::abs(sample.wi.dot(surface.shadingNormal));
                        albedoSum = albedoSum + sample.f * (cosine / sample.pdf);
                    }
                }
            }
            if (state.bounce == 0)
                normalSum = normalSum + surface.shadingNormal;
            PathIntegrator::Impl{}.onSurfaceHit(state, surface);
        }
    }

    params.film.scale(pixelIndex, params.getAccumulatedWeight());
    params.film.accumulate<NormalChannel>(pixel, normalSum * params.getSampleWeight());
    params.film.accumulate<AlbedoChannel>(pixel, albedoSum * params.getSampleWeight());
}
} // namespace

namespace embree::detail {
EmbreeLaunchParams const &getLaunchParams() noexcept {
    assert(currentLaunchParams && "Embree launch parameters are not bound");
    return *currentLaunchParams;
}

ScopedLaunchParams::ScopedLaunchParams(EmbreeLaunchParams const &params) noexcept
    : previous_(std::exchange(currentLaunchParams, &params)) {}

ScopedLaunchParams::~ScopedLaunchParams() { currentLaunchParams = previous_; }
} // namespace embree::detail

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

void EmbreeHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        throw std::invalid_argument("EmbreeHandler: sample batch must be nonzero");

    // Resolve the Camera, Film, Sampler, and linear pixel range for this render.
    auto const &film = product.getFilm();
    if (film.getHeight() > std::numeric_limits<std::size_t>::max() / film.getWidth())
        throw std::invalid_argument("EmbreeHandler: film dimensions are too large");
    auto const pixelCount =
        static_cast<std::size_t>(film.getWidth()) * static_cast<std::size_t>(film.getHeight());
    auto const sampler = impl_->context->getActiveSampler();
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

    // All TBB tasks share immutable launch parameters. Each task binds them to
    // thread-local storage before calling common rendering code.
    auto const params = EmbreeLaunchParams{
        .scene = &impl_->embreeContext,
        .camera = camera,
        .sampler = sampler->getImpl({film.getWidth(), film.getHeight()}),
        .accumulatedSamples = accumulatedSamples,
        .sampleOffset = impl_->sampleOffset,
        .film = entry.film,
        .batchSize = samples,
    };

    try {
        // Clear accumulation before starting work. The next batch clears
        // partial pixels after a backend failure.
        entry.accumulation.reset();
        if (entry.requestedChannels != FilmChannels::None) {
            tbb::parallel_for(
                tbb::blocked_range<std::size_t>{0, pixelCount},
                [&](tbb::blocked_range<std::size_t> const &range) {
                embree::detail::ScopedLaunchParams scope(params);
                for (auto index = range.begin(); index != range.end(); ++index)
                    renderPixel(index);
            }
            );
        }

        // Publish accumulation after all Film writes complete.
        entry.accumulation = EmbreeRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = accumulatedSamples + samples,
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
