#include <optix_device.h>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"
#include "flux/Shading/DiffuseBSDFImpl.h"

extern "C" {
__constant__ flux::OptixLaunchParams optixLaunchParams{};
}

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
        if (!optixLaunchParams.scene.intersect(state.ray, hit)) {
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
        if (!primitive.hasBSDF()) {
            if (isPrimary) {
                optixLaunchParams.film.accumulate<flux::NormalChannel>(
                    launchSample.pixel, isect.shadingNormal * sampleWeight
                );
            }
            optixLaunchParams.integrator.onSurfaceHit(state);
            break;
        }

        auto const &bsdf = optixLaunchParams.scene.getBSDF(primitive.getBSDFIndex());
        auto const continues = writesColor && optixLaunchParams.integrator.canContinue(state);
        auto const directLight = continues ? optixLaunchParams.integrator.sampleDirectLight(
                                                 state, optixLaunchParams.scene,
                                                 {
                                                     .position = isect.position,
                                                     .normal = isect.geometricNormal,
                                                 }
                                             )
                                           : flux::DirectLightSample{};
        bsdf.init(
            isect, wo,
            [&, u1 = state.sampler.get1D(),
             u2 = state.sampler.get2D()](auto const &bsdf, auto const &bsdfState) {
            if (isPrimary) {
                optixLaunchParams.film.accumulate<flux::NormalChannel>(
                    launchSample.pixel, isect.shadingNormal * sampleWeight
                );
            }

            auto const writesAlbedo =
                isPrimary && optixLaunchParams.film.hasChannel<flux::AlbedoChannel>();
            auto const query = flux::BSDFQuery{.wo = wo};

            if (!continues) {
                if (writesAlbedo) {
                    auto const sample = bsdf.sample(bsdfState, query, u1, u2);
                    optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
                        launchSample.pixel, sample.weight * sampleWeight
                    );
                }
                optixLaunchParams.integrator.onSurfaceHit(state);
                return;
            }

            auto const evaluation = directLight.pdf > 0.0F
                                        ? bsdf.evaluateAndPdf(bsdfState, query, directLight.wi)
                                        : flux::BSDFEvaluation{};
            auto const sample = bsdf.sample(bsdfState, query, u1, u2);
            if (writesAlbedo) {
                optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
                    launchSample.pixel, sample.weight * sampleWeight
                );
            }
            auto pendingShadow = flux::DirectLightCandidate{};
            if (directLight.pdf > 0.0F && evaluation.value.norm2() > 0.0F) {
                auto const mis =
                    directLight.delta
                        ? 1.0F
                        : optixLaunchParams.integrator.misWeight(directLight.pdf, evaluation.pdf);
                pendingShadow = {
                    .visibilityRay = isect.spawnRayTo(directLight.position),
                    .contribution = state.throughput * evaluation.value * directLight.radiance *
                                    (mis / directLight.pdf),
                    .valid = true,
                };
            }
            optixLaunchParams.integrator.onSurfaceHit(state, isect, sample);
            if (pendingShadow.valid &&
                optixLaunchParams.scene.isVisible(pendingShadow.visibilityRay))
                state.radiance = state.radiance + pendingShadow.contribution;
        }
        );
    }

    optixLaunchParams.film.accumulate<flux::ColorChannel>(
        launchSample.pixel, state.radiance * sampleWeight
    );
}
