#include <optix_device.h>

#include "flux/Optix/OptixContext.cuh"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Sampling/Sampler.cuh"
#include "flux/Scene/Camera.cuh"
#include "flux/Scene/Film.cuh"
#include "flux/Scene/TriangleMesh.cuh"

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
    unsigned int normalX = 0;
    unsigned int normalY = 0;
    unsigned int normalZ = 0;

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
            /* payload normalX =            */ normalX,
            /* payload normalY =            */ normalY,
            /* payload normalZ =            */ normalZ);
        // clang-format on
    }

    optixLaunchParams.film.writeNormal(
        launchIndex.x, launchIndex.y,
        {__uint_as_float(normalX), __uint_as_float(normalY), __uint_as_float(normalZ)}
    );
}

extern "C" __global__ void __miss__radiance() {}

extern "C" __global__ void __closesthit__triangle() {
    auto const instanceIndex = optixGetInstanceId();
    auto const &primitive = optixLaunchParams.scene.getPrimitive(instanceIndex);
    auto const &geometry = optixLaunchParams.scene.getGeometry(primitive.getGeometryIndex());
    auto const objectNormal = geometry.getFaceNormal(optixGetPrimitiveIndex());
    auto const normal =
        fromFloat3(optixTransformNormalFromObjectToWorldSpace(toFloat3(objectNormal))).normalize();

    optixSetPayload_0(__float_as_uint(normal.x()));
    optixSetPayload_1(__float_as_uint(normal.y()));
    optixSetPayload_2(__float_as_uint(normal.z()));
}
