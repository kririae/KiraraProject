#include <optix_device.h>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixImageTexturePool.cuh"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"
#include "flux/Shading/BSDFImpl.h"
#include "flux/Shading/Frame.h"

extern "C" {
__constant__ flux::OptixLaunchParams optixLaunchParams{};
}

namespace {
struct OptixImageTextureEvaluator {
    [[nodiscard]] KIRA_DEVICE static flux::Vec4f eval4f(
        std::uint32_t index, flux::Vec2f uv, flux::Vec2f const &duvdx, flux::Vec2f const &duvdy
    ) noexcept {
        auto const &texture = optixLaunchParams.scene.imageTexturePool.get(index);
        return texture.componentMapping.apply(texture.sample(uv, duvdx, duvdy));
    }
};
} // namespace

extern "C" __global__ void __miss__megakernel() {} // NOLINT

extern "C" __global__ void __raygen__megakernel() { // NOLINT
    auto const launchSample = optixLaunchParams.getLaunchSample(optixGetLaunchIndex().x);
    auto const resolution =
        flux::Vec2u{optixLaunchParams.film.width, optixLaunchParams.film.height};
    auto const sampleWeight = optixLaunchParams.getSampleWeight();
    auto sampler = optixLaunchParams.sampler;
    sampler.startPixelSample(launchSample.pixel, launchSample.sampleIndex, resolution);
    auto const pixelSample = sampler.getPixel2D();
    auto const rasterPosition = flux::Vec2f{
        static_cast<float>(launchSample.pixel.x()) + pixelSample.x(),
        static_cast<float>(launchSample.pixel.y()) + pixelSample.y(),
    };
    auto const ray =
        optixLaunchParams.camera.generateRay(rasterPosition, sampler.get2D(), resolution);

    auto const packedFootprint = flux::PackedRayFootprint::pack(
        optixLaunchParams.camera.getRayFootprint(ray.direction, resolution)
    );
    flux::PathState state{.ray = ray, .sampler = sampler, .footprint = packedFootprint};
    auto const writesColor = optixLaunchParams.film.hasChannel<flux::ColorChannel>();

    while (state.active) {
        flux::OptixContext::Impl::Hit hit;
        // Reorder on every hit, primary ones included. Guarding this on depth would give the
        // shading two static predecessors, one per continuation, and the compiler would emit a
        // second copy of it. Once paths are regenerated the guard would also stop being
        // warp-uniform, which would split the warp across two continuations for good.
        if (!optixLaunchParams.scene.intersect(state.ray, hit, optixLaunchParams.shaderReorder)) {
            if (writesColor && optixLaunchParams.hasEnvMap) {
                optixLaunchParams.integrator.onMiss<OptixImageTextureEvaluator>(
                    state, optixLaunchParams.scene
                );
            } else {
                optixLaunchParams.integrator.onMiss(state);
            }
            break;
        }

        auto const &isect = hit.surface;
        // NVCC generates a faster path when the unpacked footprint stays in this scope.
        auto footprint = state.footprint.unpack();
        footprint.propagate(optixHitObjectGetRayTmax());
        auto const &primitive = optixLaunchParams.scene.getPrimitive(isect.primitiveIndex);
        auto const isPrimary = state.depth == 0;
        auto const wo = -state.ray.direction;
        if (writesColor)
            optixLaunchParams.integrator.onEmitterHit(
                state, optixLaunchParams.scene, primitive, isect, wo
            );
        if (isPrimary) {
            optixLaunchParams.film.accumulate<flux::NormalChannel>(
                launchSample.pixel, isect.shadingNormal * sampleWeight
            );
        }
        if (!primitive.hasBSDF()) {
            optixLaunchParams.integrator.onSurfaceHit(state);
            break;
        }

        auto const writesAlbedo =
            isPrimary && optixLaunchParams.film.hasChannel<flux::AlbedoChannel>();
        auto const continues = writesColor && optixLaunchParams.integrator.canContinue(state);
        if (!continues && !writesAlbedo) {
            optixLaunchParams.integrator.onSurfaceHit(state);
            break;
        }

        auto directLight = flux::DirectLightSample{};
        if (continues) {
            directLight =
                optixLaunchParams.integrator.sampleDirectLight<OptixImageTextureEvaluator>(
                    state, optixLaunchParams.scene,
                    {
                        .position = isect.position,
                        .normal = isect.geometricNormal,
                    },
                    optixLaunchParams.hasEnvMap
                );
        }

        auto candidate = flux::DirectLightCandidate{};
        {
            auto const frame = flux::Frame{isect.shadingNormal};
            auto const localWo = frame.toLocal(wo);
            auto const localLightWi = frame.toLocal(directLight.wi);
            auto const u1 = state.sampler.get1D();
            auto const u2 = state.sampler.get2D();
            auto const &bsdf = optixLaunchParams.scene.getBSDF(primitive.getBSDFIndex());
            auto const texCtx = optixLaunchParams.scene.getTextureEvalContext(
                isect, state.ray.direction, footprint
            );
            auto const result =
                optixLaunchParams.bsdfDispatcher.execute<OptixImageTextureEvaluator>(
                    bsdf, texCtx, localWo, localLightWi, directLight.pdf > 0.0F, u1, u2
                );

            if (writesAlbedo) {
                optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
                    launchSample.pixel, result.sample.weight * sampleWeight
                );
            }
            if (!continues) {
                optixLaunchParams.integrator.onSurfaceHit(state);
                break;
            }

            candidate = optixLaunchParams.integrator.makeDirectLightCandidate(
                state.throughput, directLight, result.evaluation
            );
            optixLaunchParams.integrator.onSurfaceHit(
                state, isect, frame.toWorld(result.sample.wi), result.sample
            );
            if (state.active)
                optixLaunchParams.integrator.updateFootprint(state, result.sample, footprint);
        }

        // Trace the shadow ray on every lane that reaches here, candidate or not. OptiX resumes
        // a lane at the continuation of the traversal it suspended at, so a lane that skipped
        // this call would suspend at the next extension traversal instead and resume in a
        // different continuation from the rest of its warp, with nothing to merge the two groups
        // again. Lanes without a candidate pass mask 0, which visits no instance. Reusing
        // state.ray keeps no extra value live across the traversal.
        auto shadowRay = state.ray;
        auto mask = 0U;
        if (candidate.valid) {
            shadowRay = directLight.type == flux::LightType::EnvMap
                            ? isect.spawnRay(directLight.wi)
                            : isect.spawnRayTo(directLight.position);
            mask = 255U;
        }
        // Bind the result before testing it. Short-circuiting the traversal would split the warp.
        auto const visible = optixLaunchParams.scene.isVisible(shadowRay, mask);
        if (candidate.valid && visible)
            state.radiance = state.radiance + candidate.contribution;
    }

    optixLaunchParams.film.accumulate<flux::ColorChannel>(
        launchSample.pixel, state.radiance * sampleWeight
    );
}
