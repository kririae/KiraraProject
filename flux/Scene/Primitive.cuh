#pragma once

#include "flux/Scene/Primitive.h"

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
} // namespace flux
