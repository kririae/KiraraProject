#include <optix_device.h>

#include <cmath>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixInteraction.cuh"
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

    while (state.active) {
        if (optixLaunchParams.scene.traversable)
            optixLaunchParams.scene.trace(state);
        else
            optixLaunchParams.integrator.onMiss(state);
        if (state.hasPendingShadowQuery) {
            auto const visible = optixLaunchParams.scene.isVisible(state.pendingShadowQuery.ray);
            optixLaunchParams.integrator.resolvePendingShadowQuery(state, visible);
        }
    }

    optixLaunchParams.film.accumulate<flux::ColorChannel>(
        launchSample.pixel, state.radiance * optixLaunchParams.getSampleWeight()
    );
}

extern "C" __global__ void __miss__radiance() { // NOLINT
    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    optixLaunchParams.integrator.onMiss(*state);
}

extern "C" __global__ void __miss__shadow() { // NOLINT
    optixSetPayload_0(0);
}

extern "C" __global__ void __closesthit__triangle_diffuse() { // NOLINT
    auto const primitiveIndex = optixGetInstanceId();
    auto const &primitive = optixLaunchParams.scene.getPrimitive(primitiveIndex);
    auto const &geometry = optixLaunchParams.scene.getGeometry(primitive.getGeometryIndex());
    auto const barycentrics = optixGetTriangleBarycentrics();
    auto const preliminary = flux::PreliminaryIntersection{
        .distance = optixGetRayTmax(),
        .coordinates = {barycentrics.x, barycentrics.y},
        .elementIndex = optixGetPrimitiveIndex(),
    };
    auto surface = flux::optix::makeSurfaceInteraction(geometry, preliminary, primitiveIndex);
    auto const rayDirectionValue = optixGetWorldRayDirection();
    auto const rayDirection =
        flux::Vec3f{rayDirectionValue.x, rayDirectionValue.y, rayDirectionValue.z};

    auto *state = flux::optix::getPayloadPointer<flux::PathState>();

    if (state->depth == 0) {
        auto const launchSample = optixLaunchParams.getLaunchSample(optixGetLaunchIndex().x);
        optixLaunchParams.film.accumulate<flux::NormalChannel>(
            launchSample.pixel, surface.shadingNormal * optixLaunchParams.getSampleWeight()
        );
    }

    if (primitive.hasBSDF()) {
        auto const &bsdf = optixLaunchParams.scene.getBSDF(primitive.getBSDFIndex());
        auto diffuse = bsdf.get<flux::DiffuseBSDF::Impl>();
        auto const wo = -rayDirection;
        diffuse.init(surface, wo);

        if (state->depth == 0) {
            auto const launchSample = optixLaunchParams.getLaunchSample(optixGetLaunchIndex().x);
            auto const sampleWeight = optixLaunchParams.getSampleWeight();
            if (optixLaunchParams.film.hasChannel<flux::AlbedoChannel>()) {
                auto aovSampler = state->sampler;
                auto const query = flux::BSDFQuery{.surface = surface, .wo = wo};
                auto const bsdfSample =
                    diffuse.sample(query, aovSampler.get1D(), aovSampler.get2D());
                if (bsdfSample.pdf > 0.0F) {
                    auto const cosine = std::abs(bsdfSample.wi.dot(surface.shadingNormal));
                    auto const albedo = bsdfSample.f * (cosine / bsdfSample.pdf);
                    optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
                        launchSample.pixel, albedo * sampleWeight
                    );
                }
            }
        }

        if (optixLaunchParams.film.hasChannel<flux::ColorChannel>())
            optixLaunchParams.integrator.onSurfaceHit(
                *state, optixLaunchParams.scene, diffuse, surface, wo
            );
        else
            optixLaunchParams.integrator.onSurfaceHit(*state, surface);
    } else {
        optixLaunchParams.integrator.onSurfaceHit(*state, surface);
    }
}

extern "C" __global__ void __closesthit__triangle_shadow() { // NOLINT
    optixSetPayload_0(1);
}
