#include <optix_device.h>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixContext.cuh"
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

    flux::PathState state{
        .ray = ray,
        .sampler = sampler,
    };
    auto const writesColor = optixLaunchParams.film.hasChannel<flux::ColorChannel>();

    while (state.active) {
        flux::OptixContext::Impl::Hit hit;
        auto const reorder = optixLaunchParams.shaderReorder && state.depth > 0;
        if (!optixLaunchParams.scene.intersect(state.ray, hit, reorder)) {
            optixLaunchParams.integrator.onMiss(state);
            break;
        }

        auto const &isect = hit.surface;
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
            directLight = optixLaunchParams.integrator.sampleDirectLight(
                state, optixLaunchParams.scene,
                {
                    .position = isect.position,
                    .normal = isect.geometricNormal,
                }
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
            auto const result = optixLaunchParams.bsdfDispatcher.execute(
                bsdf, isect, localWo, localLightWi, directLight.pdf > 0.0F, u1, u2
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
        }

        if (candidate.valid &&
            optixLaunchParams.scene.isVisible(isect.spawnRayTo(directLight.position)))
            state.radiance = state.radiance + candidate.contribution;
    }

    optixLaunchParams.film.accumulate<flux::ColorChannel>(
        launchSample.pixel, state.radiance * sampleWeight
    );
}
