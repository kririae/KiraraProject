#include <optix_device.h>

#include <cmath>

#include "flux/Integrator/PathIntegratorImpl.h"
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

    // The megakernel schedules each path. Every integrator call advances one
    // vertex.
    while (state.active)
        optixLaunchParams.scene.trace(state);
}

extern "C" __global__ void __miss__radiance() { // NOLINT
    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    flux::PathIntegrator::Impl{}.onMiss(*state);
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

    auto const launchSample = optixLaunchParams.getLaunchSample(optixGetLaunchIndex().x);
    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    auto const sampleWeight = optixLaunchParams.getSampleWeight();

    if (primitive.hasBSDF()) {
        // The SBT selects the diffuse hit program. The primitive stores its
        // OptiX scene BSDF index.
        auto const &bsdf = optixLaunchParams.scene.getBSDF(primitive.getBSDFIndex());
        auto diffuse = bsdf.get<flux::DiffuseBSDF::Impl>();
        auto const wo = -rayDirection;
        diffuse.init(surface, wo);

        if (state->bounce == 0) {
            // BSDF initialization may change the shading normal. Write the
            // result to the normal channel.
            optixLaunchParams.film.accumulate<flux::NormalChannel>(
                launchSample.pixel, surface.shadingNormal * sampleWeight
            );

            if (optixLaunchParams.film.hasChannel<flux::AlbedoChannel>()) {
                // The albedo channel uses the BSDF sample. Diffuse reflection
                // yields its constant reflectance.
                auto const query = flux::BSDFQuery{.surface = surface, .wo = wo};
                auto const bsdfSample =
                    diffuse.sample(query, state->sampler.get1D(), state->sampler.get2D());
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

    flux::PathIntegrator::Impl{}.onSurfaceHit(*state, surface);
}

extern "C" __global__ void __closesthit__triangle_shadow() { // NOLINT
    optixSetPayload_0(1);
}
