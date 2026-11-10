#include "flux/Embree/EmbreeMegaKernel.h"

#include <cstdint>
#include <cstdlib>

#include "flux/Embree/EmbreeLaunchParams.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Shading/BSDFImpl.h"
#include "flux/Shading/Frame.h"

namespace flux::embree {
struct EmbreeEvaluator {
    /// Never runs: sync throws on a scene with a shader BSDF.
    [[noreturn]] static BSDFResult
    execute(std::uint32_t, ShadingContext const &, Vec3f const &, Vec3f const &, bool, float, Vec2f const &) noexcept {
        std::abort();
    }

    /// Never runs: sync throws on a scene with a shader EDF.
    [[noreturn]] static Spectrum
    evaluate(std::uint32_t, Vec2f const &, Vec3f const &, Vec3f const &) noexcept {
        std::abort();
    }

    [[nodiscard]] static Vec4f
    eval4f(std::uint32_t index, Vec2f uv, Vec2f const &duvdx, Vec2f const &duvdy) noexcept {
        return getLaunchParams().scene.imageTexturePool.eval4f(index, uv, duvdx, duvdy);
    }
};

void runMegaKernel(EmbreeLaunchParams const &params, std::size_t linearIndex) noexcept {
    constexpr auto bsdfDispatcher = BSDF::Dispatcher{.types = allBSDFTypes};
    constexpr auto edfDispatcher = EDF::Dispatcher{.types = edfTypeBit(EDFType::Constant)};
    auto const resolution = Vec2u{params.film.width, params.film.height};
    auto const pixel = Vec2u{
        static_cast<std::uint32_t>(linearIndex % params.film.width),
        static_cast<std::uint32_t>(linearIndex / params.film.width),
    };
    Vec3f colorSum{};
    Vec3f normalSum{};
    Vec3f albedoSum{};
    Vec2f depthSum{};
    auto const writesColor = params.film.hasChannel<ColorChannel>();

    for (std::uint32_t batchIndex = 0; batchIndex < params.batchSize; ++batchIndex) {
        auto sampler = params.sampler;
        sampler.startPixelSample(pixel, params.getSampleIndex(batchIndex), resolution);
        auto const pixelSample = sampler.get2D(SampleUse::Pixel, 0);
        auto const rasterPosition = Vec2f{
            static_cast<float>(pixel.x()) + pixelSample.x(),
            static_cast<float>(pixel.y()) + pixelSample.y(),
        };
        auto const ray = params.camera.generateRay(
            rasterPosition, sampler.get2D(SampleUse::Lens, 0), resolution
        );
        auto const packedFootprint =
            PackedRayFootprint::pack(params.camera.getRayFootprint(ray.direction, resolution));
        PathState state{
            .rayDirection = ray.direction,
            .sampler = sampler,
            // The camera vertex has no surface, so the derived ray keeps this origin unoffset.
            .prevLightCtx = {.position = ray.origin},
            .footprint = packedFootprint,
        };

        while (state.active) {
            EmbreeContext::Hit hit;
            auto const traced = state.getRay();
            if (!params.scene.intersect(traced, hit)) {
                if (writesColor)
                    params.integrator.onMiss<EmbreeEvaluator>(state, params.scene);
                else
                    params.integrator.onMiss(state);
                break;
            }

            auto const isect = params.scene.makeSurfaceInteraction(traced, hit);
            auto const &primitive = params.scene.table.getPrimitive(hit.primitiveIndex);
            auto const isPrimary = state.depth == 0;
            auto const wo = -traced.direction;
            if (writesColor)
                params.integrator.onEmitterHit<EmbreeEvaluator>(
                    state, params.scene, edfDispatcher, primitive, isect, wo
                );
            if (isPrimary) {
                normalSum = normalSum + isect.shadingNormal;
                depthSum = depthSum + Vec2f{params.camera.getDepth(isect.position), 1.0F};
            }
            if (!primitive.hasBSDF()) {
                params.integrator.onSurfaceHit(state);
                break;
            }

            auto const writesAlbedo = isPrimary && params.film.hasChannel<AlbedoChannel>();
            auto const continues = writesColor && params.integrator.canContinue(state);
            if (!continues && !writesAlbedo) {
                params.integrator.onSurfaceHit(state);
                break;
            }

            auto directLight = DirectLightSample{};
            if (continues) {
                directLight = params.integrator.sampleDirectLight<EmbreeEvaluator>(
                    state, params.scene, edfDispatcher,
                    {
                        .position = isect.position,
                        .normal = isect.geometricNormal,
                    },
                    params.scene.lightSampler.table.envMap != nullptr
                );
            }

            auto candidate = DirectLightCandidate{};
            {
                auto const frame = Frame{isect.shadingNormal};
                auto const localWo = frame.toLocal(wo);
                auto const localLightWi = frame.toLocal(directLight.wi);
                auto const u = state.sampler.get3D(SampleUse::Bsdf, state.depth);
                auto const uLobe = u.x();
                auto const uDirection = Vec2f{u.y(), u.z()};
                auto const &bsdf = params.scene.table.getBSDF(primitive.getBSDFIndex());
                auto footprint = state.footprint.unpack();
                footprint.propagate(hit.preliminary.distance);
                auto const ctx = makeShadingContext(isect, traced.direction, footprint);
                auto const result = bsdfDispatcher.execute<EmbreeEvaluator>(
                    bsdf, ctx, localWo, localLightWi, directLight.pdf > 0.0F, uLobe, uDirection
                );

                if (writesAlbedo)
                    albedoSum = albedoSum + result.sample.weight;
                if (!continues) {
                    params.integrator.onSurfaceHit(state);
                    break;
                }

                candidate = params.integrator.makeDirectLightCandidate(
                    state.throughput, directLight, result.evaluation
                );
                params.integrator.onSurfaceHit(
                    state, isect, frame.toWorld(result.sample.wi), result.sample
                );
                if (state.active)
                    params.integrator.updateFootprint(state, result.sample, footprint);
            }

            if (!candidate.valid)
                continue;

            auto const shadowRay = directLight.type == LightType::EnvMap
                                       ? isect.spawnRay(directLight.wi)
                                       : isect.spawnRayTo(directLight.position);
            if (params.scene.isVisible(shadowRay))
                state.radiance = state.radiance + candidate.contribution;
        }

        colorSum = colorSum + state.radiance;
    }

    if (params.accumulatedSamples != 0)
        params.film.scale(linearIndex, params.getAccumulatedWeight());
    auto const sampleWeight = params.getSampleWeight();
    params.film.accumulate<ColorChannel>(pixel, colorSum * sampleWeight);
    params.film.accumulate<NormalChannel>(pixel, normalSum * sampleWeight);
    params.film.accumulate<AlbedoChannel>(pixel, albedoSum * sampleWeight);
    params.film.accumulate<DepthChannel>(pixel, depthSum * sampleWeight);
}
} // namespace flux::embree
