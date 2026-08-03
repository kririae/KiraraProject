#include <optix_device.h>

#include <cmath>

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

        auto &surface = hit.surface;
        auto const &primitive = optixLaunchParams.scene.getPrimitive(surface.primitiveIndex);
        auto const isPrimary = state.depth == 0;

        if (isPrimary) {
            optixLaunchParams.film.accumulate<flux::NormalChannel>(
                launchSample.pixel, surface.shadingNormal * sampleWeight
            );
        }

        auto const wo = -state.ray.direction;
        if (!primitive.hasBSDF()) {
            if (writesColor)
                optixLaunchParams.integrator.onEmitterHit(
                    state, optixLaunchParams.scene, primitive, surface, wo
                );
            optixLaunchParams.integrator.onSurfaceHit(state, surface);
            break;
        }

        auto const &bsdf = optixLaunchParams.scene.getBSDF(primitive.getBSDFIndex());
        bsdf.init(surface, wo);

        if (isPrimary && optixLaunchParams.film.hasChannel<flux::AlbedoChannel>()) {
            auto aovSampler = state.sampler;
            auto const query = flux::BSDFQuery{.surface = surface, .wo = wo};
            auto const bsdfSample = bsdf.sample(query, aovSampler.get1D(), aovSampler.get2D());
            if (bsdfSample.pdf > 0.0F) {
                auto const cosTheta = std::abs(bsdfSample.wi.dot(surface.shadingNormal));
                auto const albedo = bsdfSample.f * (cosTheta / bsdfSample.pdf);
                optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
                    launchSample.pixel, albedo * sampleWeight
                );
            }
        }

        if (!writesColor) {
            optixLaunchParams.integrator.onSurfaceHit(state, surface);
            break;
        }

        optixLaunchParams.integrator.onEmitterHit(
            state, optixLaunchParams.scene, primitive, surface, wo
        );

        auto const directLight = optixLaunchParams.integrator.onSurfaceHit(
            state, optixLaunchParams.scene, bsdf, surface, wo
        );
        if (directLight.valid && optixLaunchParams.scene.isVisible(directLight.visibilityRay))
            state.radiance = state.radiance + directLight.contribution;
    }

    optixLaunchParams.film.accumulate<flux::ColorChannel>(
        launchSample.pixel, state.radiance * sampleWeight
    );
}
