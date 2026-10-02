#pragma once

#include <optix_device.h>

#include <cstdint>

#include "flux/Core/MathUtils.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixInteraction.cuh"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/LightSamplingImpl.h"
#include "flux/Shading/EDF.h"

namespace flux {
KIRA_DEVICE inline bool
OptixContext::Impl::intersect(Ray const &ray, Hit &hit, bool shaderReorder) const noexcept {
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
    if (shaderReorder) {
        // Sort by the outgoing hit object alone. It already carries the SBT record, so a
        // coherence hint built from that record would only take sort bits away from it.
        optixReorder();
    }

    // Capture the world-space interaction while the outgoing hit object provides its transform.
    auto const primitiveIndex = optixHitObjectGetInstanceId();
    auto const &primitive = table.getPrimitive(primitiveIndex);
    auto const &geometry = table.getGeometry(primitive.getGeometryIndex());
    auto const barycentrics = optixHitObjectGetTriangleBarycentrics();
    auto const preliminary = PreliminaryIntersection{
        .distance = optixHitObjectGetRayTmax(),
        .coordinates = {barycentrics.x, barycentrics.y},
        .elementIndex = optixHitObjectGetPrimitiveIndex(),
    };
    hit = {
        .surface = optix::makeSurfaceInteraction(geometry, preliminary, primitiveIndex, ray),
    };
    return true;
}

KIRA_DEVICE inline bool
OptixContext::Impl::isVisible(Ray const &ray, unsigned int mask) const noexcept {
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
        /* visibilityMask =  */ mask,
        /* rayFlags =        */ OPTIX_RAY_FLAG_DISABLE_ANYHIT |
                               OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT |
                               OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
        /* sbtOffset =       */ 0,
        /* sbtStride =       */ 1,
        /* missSbtIndex =    */ 0);
    // clang-format on
    return !optixHitObjectIsHit();
}

KIRA_DEVICE inline Vec3f OptixContext::Impl::transformPointToWorld(
    std::uint32_t primitiveIndex, Vec3f const &point
) const noexcept {
    auto const instance =
        optixGetInstanceTraversableFromIAS(traversable, instanceIndices[primitiveIndex]);
    auto const *transform = optixGetInstanceTransformFromHandle(instance);
    return transformPoint(transform, point);
}

KIRA_DEVICE inline Vec3f OptixContext::Impl::transformNormalToWorld(
    std::uint32_t primitiveIndex, Vec3f const &normal
) const noexcept {
    auto const instance =
        optixGetInstanceTraversableFromIAS(traversable, instanceIndices[primitiveIndex]);
    auto const *transform = optixGetInstanceInverseTransformFromHandle(instance);
    return transformTransposeVec(transform, normal);
}

KIRA_DEVICE inline float OptixContext::Impl::pdfDirectLight(
    LightSamplingContext const &ctx, SurfaceInteraction const &isect
) const noexcept {
    return flux::pdfDirectLight(*this, ctx, isect);
}

} // namespace flux
