#include "flux/Embree/EmbreeMegaKernel.h"

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
    auto const writesColor = params.film.hasChannel<ColorChannel>();

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

        while (state.active) {
            EmbreeContext::Hit hit;
            if (!params.scene.intersect(state.ray, hit)) {
                params.integrator.onMiss(state);
                break;
            }

            auto const isect = params.scene.makeSurfaceInteraction(state.ray, hit);
            auto const &primitive = params.scene.getPrimitive(hit.primitiveIndex);
            auto const isPrimary = state.depth == 0;
            auto const wo = -state.ray.direction;
            if (writesColor)
                params.integrator.onEmitterHit(state, params.scene, primitive, isect, wo);
            if (!primitive.hasBSDF()) {
                if (isPrimary)
                    normalSum = normalSum + isect.shadingNormal;
                params.integrator.onSurfaceHit(state);
                break;
            }

            auto const &bsdf = params.scene.getBSDF(primitive.getBSDFIndex());
            auto const continues = writesColor && params.integrator.canContinue(state);
            auto const directLight = continues ? params.integrator.sampleDirectLight(
                                                     state, params.scene,
                                                     {
                                                         .position = isect.position,
                                                         .normal = isect.geometricNormal,
                                                     }
                                                 )
                                               : DirectLightSample{};
            bsdf.init(
                isect, wo,
                [&, u1 = state.sampler.get1D(),
                 u2 = state.sampler.get2D()](auto const &bsdf, auto const &bsdfState) {
                if (isPrimary)
                    normalSum = normalSum + isect.shadingNormal;

                auto const writesAlbedo = isPrimary && params.film.hasChannel<AlbedoChannel>();
                auto const query = BSDFQuery{.wo = wo};

                if (!continues) {
                    if (writesAlbedo) {
                        auto const sample = bsdf.sample(bsdfState, query, u1, u2);
                        albedoSum = albedoSum + sample.weight;
                    }
                    params.integrator.onSurfaceHit(state);
                    return;
                }

                auto const evaluation = directLight.pdf > 0.0F
                                            ? bsdf.evaluateAndPdf(bsdfState, query, directLight.wi)
                                            : BSDFEvaluation{};
                auto const sample = bsdf.sample(bsdfState, query, u1, u2);
                if (writesAlbedo)
                    albedoSum = albedoSum + sample.weight;
                auto pendingShadow = DirectLightCandidate{};
                if (directLight.pdf > 0.0F && evaluation.value.norm2() > 0.0F) {
                    auto const mis =
                        directLight.delta
                            ? 1.0F
                            : params.integrator.misWeight(directLight.pdf, evaluation.pdf);
                    pendingShadow = {
                        .visibilityRay = isect.spawnRayTo(directLight.position),
                        .contribution = state.throughput * evaluation.value * directLight.radiance *
                                        (mis / directLight.pdf),
                        .valid = true,
                    };
                }
                params.integrator.onSurfaceHit(state, isect, sample);
                if (pendingShadow.valid && params.scene.isVisible(pendingShadow.visibilityRay))
                    state.radiance = state.radiance + pendingShadow.contribution;
            }
            );
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
