#pragma once

#include <optix_device.h>

#include <cstdint>

#include "flux/Integrator/PathIntegratorImpl.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Scene/PrimitiveImpl.h"
#include "flux/Scene/TriangleMeshImpl.h"

namespace flux {
namespace optix {
static_assert(sizeof(std::uintptr_t) == 2 * sizeof(std::uint32_t));

/// \brief Packs a device pointer into two OptiX payload registers.
///
/// \tparam T Pointed-to type.
/// \param pointer Device pointer to encode.
/// \param upper Receives the most significant 32 bits.
/// \param lower Receives the least significant 32 bits.
template <typename T>
KIRA_DEVICE void
packPayloadPointer(T *pointer, std::uint32_t &upper, std::uint32_t &lower) noexcept {
    auto const value = reinterpret_cast<std::uintptr_t>(pointer);
    upper = static_cast<std::uint32_t>(value >> 32U);
    lower = static_cast<std::uint32_t>(value);
}

/// \brief Returns the pointer stored in the first two OptiX payload registers.
///
/// \tparam T Pointed-to type.
template <typename T> [[nodiscard]] KIRA_DEVICE T *getPayloadPointer() noexcept {
    auto const value = static_cast<std::uintptr_t>(optixGetPayload_0()) << 32U |
                       static_cast<std::uintptr_t>(optixGetPayload_1());
    return reinterpret_cast<T *>(value);
}
} // namespace optix

KIRA_DEVICE inline void OptixContext::DeviceImpl::trace(PathState &state) const noexcept {
    if (!traversable) {
        PathIntegrator::Impl{}.onMiss(state);
        return;
    }

    std::uint32_t payloadUpper = 0;
    std::uint32_t payloadLower = 0;
    optix::packPayloadPointer(&state, payloadUpper, payloadLower);

    // clang-format off
    optixTrace(
        /* handle =          */ traversable,
        /* rayOrigin =       */ make_float3(state.ray.origin.x(), state.ray.origin.y(), state.ray.origin.z()),
        /* rayDirection =    */ make_float3(state.ray.direction.x(), state.ray.direction.y(), state.ray.direction.z()),
        /* tmin =            */ state.ray.minDistance,
        /* tmax =            */ state.ray.maxDistance,
        /* rayTime =         */ 0.0F,
        /* visibilityMask =  */ 255,
        /* rayFlags =        */ OPTIX_RAY_FLAG_DISABLE_ANYHIT,
        /* sbtOffset =       */ static_cast<unsigned int>(RayType::Radiance),
        /* sbtStride =       */ static_cast<unsigned int>(RayType::Count),
        /* missSbtIndex =    */ static_cast<unsigned int>(RayType::Radiance),
        /* payload upper =   */ payloadUpper,
        /* payload lower =   */ payloadLower);
    // clang-format on
}

KIRA_DEVICE inline Primitive::Impl const &
OptixContext::DeviceImpl::getPrimitive(std::uint32_t instanceIndex) const noexcept {
    return primitives[instanceIndex];
}

KIRA_DEVICE inline TriangleMesh::Impl const &
OptixContext::DeviceImpl::getGeometry(std::uint32_t geometryIndex) const noexcept {
    return geometries[geometryIndex];
}

KIRA_DEVICE inline BSDF::Impl const &
OptixContext::DeviceImpl::getBSDF(std::uint32_t bsdfIndex) const noexcept {
    return bsdfs[bsdfIndex];
}
} // namespace flux
