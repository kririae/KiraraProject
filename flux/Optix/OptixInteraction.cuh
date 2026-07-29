#pragma once

#include <optix_device.h>

#include <cstdint>

#include "flux/Scene/Geometry.h"
#include "flux/Shading/Interaction.h"

namespace flux::optix {
/// \brief Reconstructs a world-space interaction for the current OptiX hit.
///
/// Geometry computes geometry-space fields. OptiX transforms normals and reads
/// the world ray from the current hit.
template <typename GeometryImpl>
[[nodiscard]] KIRA_DEVICE inline SurfaceInteraction makeSurfaceInteraction(
    GeometryImpl const &geometry, PreliminaryIntersection const &preliminary,
    std::uint32_t primitiveIndex
) noexcept {
    auto const geometryInteraction = geometry.computeInteraction(preliminary);
    auto const geometricNormalValue = optixTransformNormalFromObjectToWorldSpace(make_float3(
        geometryInteraction.geometricNormal.x(), geometryInteraction.geometricNormal.y(),
        geometryInteraction.geometricNormal.z()
    ));
    auto const shadingNormalValue = optixTransformNormalFromObjectToWorldSpace(make_float3(
        geometryInteraction.shadingNormal.x(), geometryInteraction.shadingNormal.y(),
        geometryInteraction.shadingNormal.z()
    ));
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
        .uv = geometryInteraction.uv,
        .primitiveIndex = primitiveIndex,
        .elementIndex = geometryInteraction.elementIndex,
        .distance = preliminary.distance,
    };
}
} // namespace flux::optix
