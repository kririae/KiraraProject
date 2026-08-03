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

KIRA_HOST_DEVICE inline bool Primitive::Impl::hasEDF() const noexcept {
    return edfIndex != invalidEDFIndex;
}

KIRA_HOST_DEVICE inline std::uint32_t Primitive::Impl::getEDFIndex() const noexcept {
    return edfIndex;
}

KIRA_HOST_DEVICE inline bool Primitive::Impl::isLight() const noexcept {
    return lightIndex != invalidLightIndex;
}

KIRA_HOST_DEVICE inline std::uint32_t Primitive::Impl::getLightIndex() const noexcept {
    return lightIndex;
}
} // namespace flux
