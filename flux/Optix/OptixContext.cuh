#pragma once

#include "flux/Optix/OptixContext.h"
#include "flux/Scene/Primitive.cuh"
#include "flux/Scene/TriangleMesh.cuh"

namespace flux {
KIRA_DEVICE inline Primitive::DeviceImpl const &
OptixContext::DeviceImpl::getPrimitive(std::uint32_t instanceIndex) const noexcept {
    return primitives[instanceIndex];
}

KIRA_DEVICE inline TriangleMesh::DeviceImpl const &
OptixContext::DeviceImpl::getGeometry(std::uint32_t geometryIndex) const noexcept {
    return geometries[geometryIndex];
}
} // namespace flux
