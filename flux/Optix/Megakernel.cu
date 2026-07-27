#include <optix_device.h>

#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixLaunchParams.h"

extern "C" {
__constant__ flux::OptixLaunchParams optixLaunchParams;
}

namespace {
__device__ float3 toFloat3(flux::Vec3f const &value) {
    return make_float3(value.x(), value.y(), value.z());
}
} // namespace

extern "C" __global__ void __raygen__megakernel() {
    auto const index = optixGetLaunchIndex().x;
    if (index >= optixLaunchParams.rayCount || !optixLaunchParams.rays || !optixLaunchParams.hits)
        return;

    auto const &ray = optixLaunchParams.rays[index];
    unsigned int hit = 0;
    unsigned int distance = 0;
    unsigned int triangleIndex = 0;
    unsigned int instanceIndex = 0;
    unsigned int geometryIndex = 0;

    if (optixLaunchParams.scene.traversable) {
        // clang-format off
        optixTrace(
            /* handle =                     */ optixLaunchParams.scene.traversable,
            /* rayOrigin =                  */ toFloat3(ray.origin),
            /* rayDirection =               */ toFloat3(ray.direction),
            /* tmin =                       */ ray.minDistance,
            /* tmax =                       */ ray.maxDistance,
            /* rayTime =                    */ 0.0F,
            /* visibilityMask =             */ 255,
            /* rayFlags =                   */ OPTIX_RAY_FLAG_DISABLE_ANYHIT,
            /* sbtOffset =                  */ 0,
            /* sbtStride =                  */ 1,
            /* missSbtIndex =               */ 0,
            /* payload hit =                */ hit,
            /* payload distance =           */ distance,
            /* payload triangleIndex =      */ triangleIndex,
            /* payload instanceIndex =      */ instanceIndex,
            /* payload geometryIndex =      */ geometryIndex);
        // clang-format on
    }

    optixLaunchParams.hits[index] = {
        .distance = __uint_as_float(distance),
        .triangleIndex = triangleIndex,
        .instanceIndex = instanceIndex,
        .geometryIndex = geometryIndex,
        .hit = hit,
    };
}

extern "C" __global__ void __miss__intersection() {}

extern "C" __global__ void __closesthit__triangle() {
    auto const instanceIndex = optixGetInstanceId();
    auto const &primitive = optixLaunchParams.scene.getPrimitive(instanceIndex);

    optixSetPayload_0(1);
    optixSetPayload_1(__float_as_uint(optixGetRayTmax()));
    optixSetPayload_2(optixGetPrimitiveIndex());
    optixSetPayload_3(instanceIndex);
    optixSetPayload_4(primitive.getGeometryIndex());
}
