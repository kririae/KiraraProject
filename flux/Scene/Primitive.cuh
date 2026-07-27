#pragma once

#include "flux/Scene/Primitive.h"

namespace flux {
KIRA_DEVICE inline std::uint32_t Primitive::DeviceImpl::getGeometryIndex() const noexcept {
    return geometryIndex;
}
} // namespace flux
