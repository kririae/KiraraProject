#include <optix_device.h>

#include "flux/Integrator/PathIntegrator.cuh"
#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Sampling/Sampler.cuh"
#include "flux/Scene/Camera.cuh"
#include "flux/Scene/Film.cuh"

extern "C" {
__constant__ flux::OptixLaunchParams optixLaunchParams{};
}

namespace {
__device__ float3 toFloat3(flux::Vec3f const &value) {
    return make_float3(value.x(), value.y(), value.z());
}

__device__ flux::Vec3f fromFloat3(float3 const &value) { return {value.x, value.y, value.z}; }
} // namespace

extern "C" __global__ void __raygen__megakernel() {
    auto const launchIndex = optixGetLaunchIndex();
    auto const pixel = flux::Vec2u{launchIndex.x, launchIndex.y};
    auto const resolution =
        flux::Vec2u{optixLaunchParams.film.width, optixLaunchParams.film.height};
    auto sampler = optixLaunchParams.sampler;
    sampler.startPixelSample(pixel, optixLaunchParams.sampleIndex, resolution);
    auto const pixelSample = sampler.getPixel2D();
    auto const rasterPosition = flux::Vec2f{
        static_cast<float>(launchIndex.x) + pixelSample.x(),
        static_cast<float>(launchIndex.y) + pixelSample.y(),
    };
    auto const ray = optixLaunchParams.camera.generateRay(
        rasterPosition, optixLaunchParams.film.width, optixLaunchParams.film.height
    );

    // AOVs are launch outputs, not path state. Initialize the miss value before
    // traversal; a surface hit overwrites it while hit data is still local.
    flux::PathState state{.ray = ray};
    optixLaunchParams.film.writeNormal(launchIndex.x, launchIndex.y, {});

    // The megakernel owns scheduling. Integrator operations advance one path
    // vertex at a time.
    while (state.active)
        optixLaunchParams.scene.trace(state);
}

extern "C" __global__ void __miss__radiance() {
    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    flux::PathIntegrator::DeviceImpl{}.onMiss(*state);
}

extern "C" __global__ void __closesthit__triangle() {
    auto const instanceIndex = optixGetInstanceId();
    auto const &primitive = optixLaunchParams.scene.getPrimitive(instanceIndex);
    auto const &geometry = optixLaunchParams.scene.getGeometry(primitive.getGeometryIndex());
    auto const objectNormal = geometry.getFaceNormal(optixGetPrimitiveIndex());
    auto const normal =
        fromFloat3(optixTransformNormalFromObjectToWorldSpace(toFloat3(objectNormal))).normalize();
    auto const rayOrigin = fromFloat3(optixGetWorldRayOrigin());
    auto const rayDirection = fromFloat3(optixGetWorldRayDirection());
    auto const distance = optixGetRayTmax();

    // Materialize the interaction while OptiX still exposes traversal-local
    // instance, primitive, transform, and ray data.
    auto const surface = flux::SurfaceInteraction{
        .position = rayOrigin + rayDirection * distance,
        .geometricNormal = normal,
        .shadingNormal = normal,
        .primitive = &primitive,
        .primitiveIndex = optixGetPrimitiveIndex(),
        .distance = distance,
    };
    auto const launchIndex = optixGetLaunchIndex();
    optixLaunchParams.film.writeNormal(launchIndex.x, launchIndex.y, surface.geometricNormal);

    auto *state = flux::optix::getPayloadPointer<flux::PathState>();
    flux::PathIntegrator::DeviceImpl{}.onSurfaceHit(*state, surface);
}
