#pragma once

#include <optix_device.h>

#include <bit>
#include <cstdint>

#include "flux/Core/MathUtils.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixInteraction.cuh"
#include "flux/Scene/GeometryImpl.h"
#include "flux/Scene/LightSamplingImpl.h"
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
    if (!optixHitObjectIsHit())
        return false;
    if (shaderReorder) {
        // The SBT offset is a backend-assigned coherence key; material dispatch stays in raygen.
        constexpr auto numShadingPrograms = static_cast<unsigned int>(BSDFType::Count) *
                                            static_cast<unsigned int>(GeometryType::Count);
        constexpr auto hintBits = std::bit_width(numShadingPrograms - 1);
        static_assert(hintBits <= 16);
        optixReorder(optixHitObjectGetSbtRecordIndex(), hintBits);
    }

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

KIRA_DEVICE inline TextureEvalContext OptixContext::Impl::getTextureEvalContext(
    SurfaceInteraction const &isect, Vec3f const &direction, RayFootprint const &footprint
) const noexcept {
    auto dpdx = Vec3f{};
    auto dpdy = Vec3f{};
    footprint.project(direction, isect.geometricNormal, dpdx, dpdy);

    // The radiance hit is still current; shadow traversal must happen after this lookup.
    auto const dx = optixHitObjectTransformVectorFromWorldToObjectSpace(
        make_float3(dpdx.x(), dpdx.y(), dpdx.z())
    );
    auto const dy = optixHitObjectTransformVectorFromWorldToObjectSpace(
        make_float3(dpdy.x(), dpdy.y(), dpdy.z())
    );
    dpdx = Vec3f{dx.x, dx.y, dx.z};
    dpdy = Vec3f{dy.x, dy.y, dy.z};

    auto result = TextureEvalContext{.uv = isect.uv};
    auto const &primitive = getPrimitive(isect.primitiveIndex);
    getGeometry(primitive.getGeometryIndex())
        .computeTexCoordPartials(isect.elementIndex, dpdx, dpdy, result.duvdx, result.duvdy);
    return result;
}

KIRA_DEVICE inline float
OptixContext::Impl::getCurvature(SurfaceInteraction const &isect, Vec3f const &wo) const noexcept {
    auto const &prim = getPrimitive(isect.primitiveIndex);
    auto const side = wo.dot(isect.shadingNormal) < 0.0F ? -1.0F : 1.0F;
    return side * getGeometry(prim.getGeometryIndex()).getCurvature(isect.elementIndex) *
           prim.curvatureScale;
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

KIRA_DEVICE inline float OptixContext::Impl::pdfDirectLight(
    LightSamplingContext const &ctx, Primitive::Impl const &prim, SurfaceInteraction const &isect
) const noexcept {
    return flux::pdfDirectLight(*this, ctx, prim, isect);
}

} // namespace flux
