#include "flux/Embree/EmbreeMegaKernel.h"

#include <cmath>
#include <cstdint>

#include "flux/Embree/EmbreeLaunchParams.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/DiffuseBSDFImpl.h"

namespace flux::embree {
void runMegaKernel(EmbreeLaunchParams const &params, std::size_t linearIndex) noexcept {
    auto const resolution = Vec2u{params.film.width, params.film.height};
    auto const pixel = Vec2u{
        static_cast<std::uint32_t>(linearIndex % params.film.width),
        static_cast<std::uint32_t>(linearIndex / params.film.width),
    };
    Vec3f colorSum{};
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
        PathState state{};
        state.ray = params.camera.generateRay(rasterPosition, sampler.get2D(), resolution);
        state.sampler = sampler;

        while (state.active) {
            EmbreeContext::Hit hit;
            if (!params.scene.intersect(state.ray, hit)) {
                params.integrator.onMiss(state);
                continue;
            }

            auto surface = params.scene.makeSurfaceInteraction(state.ray, hit);
            auto const &primitive = params.scene.getPrimitive(hit.primitiveIndex);
            if (state.depth == 0)
                normalSum = normalSum + surface.shadingNormal;

            if (!primitive.hasBSDF()) {
                params.integrator.onSurfaceHit(state, surface);
                continue;
            }

            auto const &bsdf = params.scene.getBSDF(primitive.getBSDFIndex());
            auto const wo = -state.ray.direction;
            bsdf.init(surface, wo);

            if (state.depth == 0 && params.film.hasChannel<AlbedoChannel>()) {
                auto aovSampler = state.sampler;
                auto const query = BSDFQuery{.surface = surface, .wo = wo};
                auto const sample = bsdf.sample(query, aovSampler.get1D(), aovSampler.get2D());
                if (sample.pdf > 0.0F) {
                    auto const cosine = std::abs(sample.wi.dot(surface.shadingNormal));
                    albedoSum = albedoSum + sample.f * (cosine / sample.pdf);
                }
            }

            if (params.film.hasChannel<ColorChannel>()) {
                auto const directLight =
                    params.integrator.onSurfaceHit(state, params.scene, bsdf, surface, wo);
                if (directLight.valid && params.scene.isVisible(directLight.visibilityRay))
                    state.radiance = state.radiance + directLight.contribution;
            } else {
                params.integrator.onSurfaceHit(state, surface);
            }
        }
        colorSum = colorSum + state.radiance;
    }

    params.film.scale(linearIndex, params.getAccumulatedWeight());
    auto const sampleWeight = params.getSampleWeight();
    params.film.accumulate<ColorChannel>(pixel, colorSum * sampleWeight);
    params.film.accumulate<NormalChannel>(pixel, normalSum * sampleWeight);
    params.film.accumulate<AlbedoChannel>(pixel, albedoSum * sampleWeight);
}
} // namespace flux::embree
