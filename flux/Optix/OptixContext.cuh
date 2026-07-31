#pragma once

#include <optix_device.h>

#include <cstdint>

#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixInteraction.cuh"
#include "flux/Optix/OptixSbt.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"

namespace flux {
KIRA_DEVICE inline bool OptixContext::Impl::intersect(Ray const &ray, Hit &hit) const noexcept {
    if (!traversable)
        return false;
    // clang-format off
    optixTraverse(
        /* handle =          */ traversable,
        /* rayOrigin =       */ make_float3(ray.origin.x(), ray.origin.y(), ray.origin.z()),
        /* rayDirection =    */ make_float3(ray.direction.x(), ray.direction.y(), ray.direction.z()),
        /* tmin =            */ ray.minDistance,
        /* tmax =            */ ray.maxDistance,
        /* rayTime =         */ 0.0F,
        /* visibilityMask =  */ 255,
        /* rayFlags =        */ OPTIX_RAY_FLAG_DISABLE_ANYHIT |
                               OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
        /* sbtOffset =       */ 0,
        /* sbtStride =       */ 1,
        /* missSbtIndex =    */ 0);
    // clang-format on
    if (!optixHitObjectIsHit())
        return false;

    auto const primitiveIndex = optixHitObjectGetInstanceId();
    auto const &primitive = getPrimitive(primitiveIndex);
    auto const &geometry = getGeometry(primitive.getGeometryIndex());
    auto const barycentrics = optixHitObjectGetTriangleBarycentrics();
    auto const preliminary = PreliminaryIntersection{
        .distance = optixHitObjectGetRayTmax(),
        .coordinates = {barycentrics.x, barycentrics.y},
        .elementIndex = optixHitObjectGetPrimitiveIndex(),
    };
    auto const *data =
        reinterpret_cast<OptixHitgroupData const *>(optixHitObjectGetSbtDataPointer());
    hit = {
        .surface = optix::makeSurfaceInteraction(geometry, preliminary, primitiveIndex, ray),
        .bsdfType = data->bsdfType,
    };
    return true;
}

KIRA_DEVICE inline bool OptixContext::Impl::isVisible(Ray const &ray) const noexcept {
    if (!traversable)
        return true;

    // clang-format off
    optixTraverse(
        /* handle =          */ traversable,
        /* rayOrigin =       */ make_float3(ray.origin.x(), ray.origin.y(), ray.origin.z()),
        /* rayDirection =    */ make_float3(ray.direction.x(), ray.direction.y(), ray.direction.z()),
        /* tmin =            */ ray.minDistance,
        /* tmax =            */ ray.maxDistance,
        /* rayTime =         */ 0.0F,
        /* visibilityMask =  */ 255,
        /* rayFlags =        */ OPTIX_RAY_FLAG_DISABLE_ANYHIT |
                               OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT |
                               OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
        /* sbtOffset =       */ 0,
        /* sbtStride =       */ 1,
        /* missSbtIndex =    */ 0);
    // clang-format on
    return !optixHitObjectIsHit();
}

KIRA_DEVICE inline Primitive::Impl const &
OptixContext::Impl::getPrimitive(std::uint32_t instanceIndex) const noexcept {
    return primitives[instanceIndex];
}

KIRA_DEVICE inline TriangleMesh::Impl const &
OptixContext::Impl::getGeometry(std::uint32_t geometryIndex) const noexcept {
    return geometries[geometryIndex];
}

KIRA_DEVICE inline BSDF::Impl const &
OptixContext::Impl::getBSDF(std::uint32_t bsdfIndex) const noexcept {
    return bsdfs[bsdfIndex];
}
} // namespace flux
