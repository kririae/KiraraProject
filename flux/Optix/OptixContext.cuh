#pragma once

#include <optix_device.h>

#include <bit>
#include <cstdint>

#include "flux/Core/MathUtils.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixInteraction.cuh"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/PrimitiveImpl.h"
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
    if (shaderReorder) {
        constexpr auto numHitgroupRecords = static_cast<unsigned int>(BSDFType::Count) *
                                            static_cast<unsigned int>(GeometryType::Count);
        constexpr auto numHintBits = std::bit_width(numHitgroupRecords - 1);
        static_assert(numHintBits <= 16);

        // Reorder before materializing the hit to keep surface state out of the continuation.
        optixReorder(optixHitObjectGetSbtRecordIndex(), numHintBits);
    }
    if (!optixHitObjectIsHit())
        return false;

    // Capture the world-space interaction while the outgoing hit object provides its transform.
    auto const primitiveIndex = optixHitObjectGetInstanceId();
    auto const &primitive = getPrimitive(primitiveIndex);
    auto const &geometry = getGeometry(primitive.getGeometryIndex());
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

KIRA_DEVICE inline Vec3f OptixContext::Impl::transformPointToWorld(
    std::uint32_t primitiveIndex, Vec3f const &point
) const noexcept {
    auto const instance = optixGetInstanceTraversableFromIAS(traversable, primitiveIndex);
    auto const *transform = optixGetInstanceTransformFromHandle(instance);
    return transformPoint(transform, point);
}

KIRA_DEVICE inline Vec3f OptixContext::Impl::transformNormalToWorld(
    std::uint32_t primitiveIndex, Vec3f const &normal
) const noexcept {
    auto const instance = optixGetInstanceTraversableFromIAS(traversable, primitiveIndex);
    auto const *transform = optixGetInstanceInverseTransformFromHandle(instance);
    return transformTransposeVec(transform, normal);
}

KIRA_DEVICE inline Primitive::Impl const &
OptixContext::Impl::getPrimitive(std::uint32_t instanceIndex) const noexcept {
    return primitives[instanceIndex];
}

KIRA_DEVICE inline Geometry::Impl const &
OptixContext::Impl::getGeometry(std::uint32_t geometryIndex) const noexcept {
    return geometries[geometryIndex];
}

KIRA_DEVICE inline BSDF::Impl const &
OptixContext::Impl::getBSDF(std::uint32_t bsdfIndex) const noexcept {
    return bsdfs[bsdfIndex];
}

KIRA_DEVICE inline EDF::Impl const &
OptixContext::Impl::getEDF(std::uint32_t edfIndex) const noexcept {
    return edfs[edfIndex];
}

} // namespace flux
