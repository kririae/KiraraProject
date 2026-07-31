#pragma once

#include <limits>

#include "flux/Core/MathUtils.h"
#include "flux/Scene/TriangleMesh.h"

namespace flux {
KIRA_HOST_DEVICE inline Vec3f
TriangleMesh::Impl::getVertex(std::uint32_t triangle, std::uint32_t corner) const noexcept {
    return vertices[triangles[triangle][corner]];
}

KIRA_HOST_DEVICE inline Vec3f TriangleMesh::Impl::interpolateShadingNormal(
    PreliminaryIntersection const &preliminary, Vec3f const &geometricNormal
) const noexcept {
    if (!normals)
        return geometricNormal;

    auto const u = preliminary.coordinates.x();
    auto const v = preliminary.coordinates.y();
    auto const w = 1.0F - u - v;
    auto const indices = normalIndices[preliminary.elementIndex];
    auto const interpolated =
        normals[indices[0]] * w + normals[indices[1]] * u + normals[indices[2]] * v;
    auto const lengthSquared = interpolated.norm2();
    if (lengthSquared > 1.0e-20F && lengthSquared < std::numeric_limits<float>::max())
        return interpolated.normalize();
    return geometricNormal;
}

KIRA_HOST_DEVICE inline Vec2f
TriangleMesh::Impl::interpolateTexCoord(PreliminaryIntersection const &preliminary) const noexcept {
    if (!texCoords)
        return {};

    auto const u = preliminary.coordinates.x();
    auto const v = preliminary.coordinates.y();
    auto const w = 1.0F - u - v;
    auto const indices = texCoordIndices[preliminary.elementIndex];
    return texCoords[indices[0]] * w + texCoords[indices[1]] * u + texCoords[indices[2]] * v;
}

KIRA_HOST_DEVICE inline GeometryInteraction
TriangleMesh::Impl::computeInteraction(PreliminaryIntersection const &preliminary) const noexcept {
    auto const vertex0 = getVertex(preliminary.elementIndex, 0);
    auto const vertex1 = getVertex(preliminary.elementIndex, 1);
    auto const vertex2 = getVertex(preliminary.elementIndex, 2);
    auto const u = preliminary.coordinates.x();
    auto const v = preliminary.coordinates.y();
    auto const w = 1.0F - u - v;
    auto const edge1 = vertex1 - vertex0;
    auto const edge2 = vertex2 - vertex0;
    auto const normal = cross(edge1, edge2).normalize();

    return {
        .position = vertex0 * w + vertex1 * u + vertex2 * v,
        .geometricNormal = normal,
        .shadingNormal = interpolateShadingNormal(preliminary, normal),
        .uv = interpolateTexCoord(preliminary),
        .elementIndex = preliminary.elementIndex,
    };
}
} // namespace flux
