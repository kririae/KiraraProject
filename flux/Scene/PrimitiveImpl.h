#pragma once

#include "flux/Scene/Primitive.h"

namespace flux {
KIRA_HOST_DEVICE inline std::uint32_t Primitive::Impl::getGeometryIndex() const noexcept {
    return geometryIndex;
}

KIRA_HOST_DEVICE inline bool Primitive::Impl::hasBSDF() const noexcept {
    return bsdfIndex != invalidBSDFIndex;
}

KIRA_HOST_DEVICE inline std::uint32_t Primitive::Impl::getBSDFIndex() const noexcept {
    return bsdfIndex;
}
} // namespace flux
