#include "flux/Embree/EmbreeMegaKernel.h"

#include <cstdint>

#include "flux/Embree/EmbreeLaunchParams.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Shading/BSDFImpl.h"
#include "flux/Shading/Frame.h"

namespace flux::embree {
struct EmbreeImageTextureEvaluator {
    [[nodiscard]] static Vec4f
    eval4f(std::uint32_t index, Vec2f uv, Vec2f const &duvdx, Vec2f const &duvdy) noexcept {
        return getLaunchParams().scene.imageTexturePool.eval4f(index, uv, duvdx, duvdy);
    }
};

void runMegaKernel(EmbreeLaunchParams const &params, std::size_t linearIndex) noexcept {
    constexpr auto bsdfDispatcher = BSDF::Dispatcher{.types = allBSDFTypes};
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
        auto packedFootprint = PackedRayFootprint{};
        if (params.needsTextureFootprint) {
            packedFootprint =
                PackedRayFootprint::pack(params.camera.getRayFootprint(ray.direction, resolution));
        }
        PathState state{.ray = ray, .sampler = sampler, .footprint = packedFootprint};

        while (state.active) {
            EmbreeContext::Hit hit;
            if (!params.scene.intersect(state.ray, hit)) {
                if (writesColor)
                    params.integrator.onMiss<EmbreeImageTextureEvaluator>(
                        state, params.scene, params.needsTextureFootprint
                    );
                else
                    params.integrator.onMiss(state);
                break;
            }

            auto const isect = params.scene.makeSurfaceInteraction(state.ray, hit);
            auto const &primitive = params.scene.getPrimitive(hit.primitiveIndex);
            auto const isPrimary = state.depth == 0;
            auto const wo = -state.ray.direction;
            if (writesColor)
                params.integrator.onEmitterHit(state, params.scene, primitive, isect, wo);
            if (isPrimary)
                normalSum = normalSum + isect.shadingNormal;
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
                directLight = params.integrator.sampleDirectLight<EmbreeImageTextureEvaluator>(
                    state, params.scene,
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
                auto const u1 = state.sampler.get1D();
                auto const u2 = state.sampler.get2D();
                auto const &bsdf = params.scene.getBSDF(primitive.getBSDFIndex());
                auto footprint = RayFootprint{};
                if (params.needsTextureFootprint && (continues || bsdf.needsTextureFootprint)) {
                    footprint = state.footprint.unpack();
                    footprint.propagate(hit.preliminary.distance);
                }
                auto texCtx = TextureEvalContext{.uv = isect.uv};
                if (bsdf.needsTextureFootprint) {
                    texCtx =
                        params.scene.getTextureEvalContext(isect, state.ray.direction, footprint);
                }
                auto const result = bsdfDispatcher.execute<EmbreeImageTextureEvaluator>(
                    bsdf, texCtx, localWo, localLightWi, directLight.pdf > 0.0F, u1, u2
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
                auto curvature = 0.0F;
                if (params.needsTextureFootprint && result.sample.needsCurvature())
                    curvature = params.scene.getCurvature(isect, wo);
                params.integrator.onSurfaceHit(
                    state, isect, frame.toWorld(result.sample.wi), result.sample
                );
                if (params.needsTextureFootprint && state.active)
                    params.integrator.updateFootprint(state, result.sample, footprint, curvature);
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

    params.film.scale(linearIndex, params.getAccumulatedWeight());
    auto const sampleWeight = params.getSampleWeight();
    params.film.accumulate<ColorChannel>(pixel, colorSum * sampleWeight);
    params.film.accumulate<NormalChannel>(pixel, normalSum * sampleWeight);
    params.film.accumulate<AlbedoChannel>(pixel, albedoSum * sampleWeight);
}
} // namespace flux::embree
