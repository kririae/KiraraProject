#pragma once

#include "flux/Scene/TriangleMesh.h"

namespace flux {
KIRA_DEVICE inline Vec3f
TriangleMesh::DeviceImpl::getVertex(std::uint32_t triangle, std::uint32_t corner) const noexcept {
    return vertices[triangles[triangle][corner]];
}

KIRA_DEVICE inline Vec3f
TriangleMesh::DeviceImpl::getFaceNormal(std::uint32_t triangle) const noexcept {
    auto const edge1 = getVertex(triangle, 1) - getVertex(triangle, 0);
    auto const edge2 = getVertex(triangle, 2) - getVertex(triangle, 0);
    return Vec3f{
        edge1.y() * edge2.z() - edge1.z() * edge2.y(),
        edge1.z() * edge2.x() - edge1.x() * edge2.z(),
        edge1.x() * edge2.y() - edge1.y() * edge2.x(),
    }
        .normalize();
}
} // namespace flux
