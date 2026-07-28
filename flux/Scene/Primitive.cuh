#pragma once

#include <optix_device.h>

#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Shading/Interaction.h"

namespace flux {
KIRA_DEVICE inline std::uint32_t Primitive::DeviceImpl::getGeometryIndex() const noexcept {
    return geometryIndex;
}

KIRA_DEVICE inline bool Primitive::DeviceImpl::hasBSDF() const noexcept {
    return bsdfIndex != invalidBSDFIndex;
}

KIRA_DEVICE inline std::uint32_t Primitive::DeviceImpl::getBSDFIndex() const noexcept {
    return bsdfIndex;
}

template <typename GeometryImpl>
KIRA_DEVICE inline SurfaceInteraction Primitive::DeviceImpl::computeSurfaceInteraction(
    GeometryImpl const &geometry, PreliminaryIntersection const &preliminary
) const noexcept {
    // TODO(wavefront): This conversion currently requires an active OptiX hit program. If
    // interaction materialization moves outside closest-hit, use a backend-owned instance
    // transform instead of carrying OptiX matrices in intersection or path state.
    auto const local = geometry.computeInteraction(preliminary);

    auto const geometricNormalValue = optixTransformNormalFromObjectToWorldSpace(
        make_float3(local.geometricNormal.x(), local.geometricNormal.y(), local.geometricNormal.z())
    );
    auto const shadingNormalValue = optixTransformNormalFromObjectToWorldSpace(
        make_float3(local.shadingNormal.x(), local.shadingNormal.y(), local.shadingNormal.z())
    );
    auto const rayOrigin = optixGetWorldRayOrigin();
    auto const rayDirection = optixGetWorldRayDirection();

    return {
        .position =
            {
                rayOrigin.x + rayDirection.x * preliminary.distance,
                rayOrigin.y + rayDirection.y * preliminary.distance,
                rayOrigin.z + rayDirection.z * preliminary.distance,
            },
        .geometricNormal =
            Vec3f{geometricNormalValue.x, geometricNormalValue.y, geometricNormalValue.z}
                .normalize(),
        .shadingNormal =
            Vec3f{shadingNormalValue.x, shadingNormalValue.y, shadingNormalValue.z}.normalize(),
        .uv = local.uv,
        .primitive = this,
        .elementIndex = local.elementIndex,
        .distance = preliminary.distance,
    };
}
} // namespace flux
