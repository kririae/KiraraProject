#include <optix_device.h>

#include <cmath>

#include "flux/Integrator/PathIntegrator.cuh"
#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Sampling/Sampler.cuh"
#include "flux/Scene/Camera.cuh"
#include "flux/Scene/Film.cuh"
#include "flux/Shading/DiffuseBSDF.cuh"

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

    // The megakernel owns scheduling. Integrator operations advance one path
    // vertex at a time.
    while (state.active)
        optixLaunchParams.scene.trace(state);
}

extern "C" __global__ void __miss__radiance() { // NOLINT
    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    flux::PathIntegrator::DeviceImpl{}.onMiss(*state);
}

extern "C" __global__ void __miss__shadow() { // NOLINT
    optixSetPayload_0(0);
}

extern "C" __global__ void __closesthit__triangle_diffuse() { // NOLINT
    auto const instanceIndex = optixGetInstanceId();
    auto const &primitive = optixLaunchParams.scene.getPrimitive(instanceIndex);
    auto const &geometry = optixLaunchParams.scene.getGeometry(primitive.getGeometryIndex());
    auto const barycentrics = optixGetTriangleBarycentrics();
    auto const preliminary = flux::PreliminaryIntersection{
        .distance = optixGetRayTmax(),
        .coordinates = {barycentrics.x, barycentrics.y},
        .elementIndex = optixGetPrimitiveIndex(),
    };
    auto surface = primitive.computeSurfaceInteraction(geometry, preliminary);
    auto const rayDirectionValue = optixGetWorldRayDirection();
    auto const rayDirection =
        flux::Vec3f{rayDirectionValue.x, rayDirectionValue.y, rayDirectionValue.z};

    auto const launchSample = optixLaunchParams.getLaunchSample(optixGetLaunchIndex().x);
    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    auto const sampleWeight = optixLaunchParams.getSampleWeight();

    if (primitive.hasBSDF()) {
        // The SBT selected Diffuse code. The primitive supplies only the
        // snapshot-local index of its parameter payload.
        auto const &payload = optixLaunchParams.scene.getBSDF(primitive.getBSDFIndex());
        auto bsdf = payload.get<flux::DiffuseBSDF::DeviceImpl>();
        auto const wo = -rayDirection;
        bsdf.init(surface, wo);

        if (state->bounce == 0) {
            // Material initialization may replace the shading normal, so guide
            // output must follow it.
            optixLaunchParams.film.accumulate<flux::NormalChannel>(
                launchSample.pixel, surface.shadingNormal * sampleWeight
            );

            if (optixLaunchParams.film.hasChannel<flux::AlbedoChannel>()) {
                // The albedo guide uses the same directional estimator as the
                // transport sample. For Lambertian reflection it is exactly
                // the constant reflectance.
                auto const query = flux::BSDFQuery{.surface = surface, .wo = wo};
                auto const bsdfSample =
                    bsdf.sample(query, state->sampler.get1D(), state->sampler.get2D());
                if (bsdfSample.pdf > 0.0F) {
                    auto const cosine = std::abs(bsdfSample.wi.dot(surface.shadingNormal));
                    auto const albedo = bsdfSample.f * (cosine / bsdfSample.pdf);
                    optixLaunchParams.film.accumulate<flux::AlbedoChannel>(
                        launchSample.pixel, albedo * sampleWeight
                    );
                }
            }
        }
    } else if (state->bounce == 0) {
        optixLaunchParams.film.accumulate<flux::NormalChannel>(
            launchSample.pixel, surface.shadingNormal * sampleWeight
        );
    }

    flux::PathIntegrator::DeviceImpl{}.onSurfaceHit(*state, surface);
}

extern "C" __global__ void __closesthit__triangle_shadow() { // NOLINT
    optixSetPayload_0(1);
}
