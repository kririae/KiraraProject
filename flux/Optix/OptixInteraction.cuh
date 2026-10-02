#pragma once

#include <optix_device.h>

#include <cstdint>

#include "flux/Scene/Geometry.h"
#include "flux/Shading/Interaction.h"

namespace flux::optix {
/// \brief Reconstructs a world-space interaction for the current OptiX hit.
///
/// Geometry reconstructs the hit in geometry space. OptiX transforms its
/// normals using the outgoing hit object.
template <typename GeometryImpl>
[[nodiscard]] KIRA_DEVICE inline SurfaceInteraction makeSurfaceInteraction(
    GeometryImpl const &geometry, PreliminaryIntersection const &preliminary,
    std::uint32_t primitiveIndex, Ray const &ray
) noexcept {
    auto const geometryInteraction = geometry.computeInteraction(preliminary);
    // A parameterization gradient maps a position offset to a uv offset, so it is a covector
    // and transforms by the inverse transpose, exactly like a normal.
    auto const uvGradU = optixHitObjectTransformNormalFromObjectToWorldSpace(make_float3(
        geometryInteraction.uvGradU.x(), geometryInteraction.uvGradU.y(),
        geometryInteraction.uvGradU.z()
    ));
    auto const uvGradV = optixHitObjectTransformNormalFromObjectToWorldSpace(make_float3(
        geometryInteraction.uvGradV.x(), geometryInteraction.uvGradV.y(),
        geometryInteraction.uvGradV.z()
    ));
    auto const geometricNormalValue =
        optixHitObjectTransformNormalFromObjectToWorldSpace(make_float3(
            geometryInteraction.geometricNormal.x(), geometryInteraction.geometricNormal.y(),
            geometryInteraction.geometricNormal.z()
        ));
    auto const shadingNormalValue = optixHitObjectTransformNormalFromObjectToWorldSpace(make_float3(
        geometryInteraction.shadingNormal.x(), geometryInteraction.shadingNormal.y(),
        geometryInteraction.shadingNormal.z()
    ));

    return {
        .position = ray.origin + ray.direction * preliminary.distance,
        .geometricNormal =
            Vec3f{geometricNormalValue.x, geometricNormalValue.y, geometricNormalValue.z}
                .normalize(),
        .shadingNormal =
            Vec3f{shadingNormalValue.x, shadingNormalValue.y, shadingNormalValue.z}.normalize(),
        .uv = geometryInteraction.uv,
        .uvGradU = {uvGradU.x, uvGradU.y, uvGradU.z},
        .uvGradV = {uvGradV.x, uvGradV.y, uvGradV.z},
        .primitiveIndex = primitiveIndex,
        .elementIndex = geometryInteraction.elementIndex,
    };
}
} // namespace flux::optix
