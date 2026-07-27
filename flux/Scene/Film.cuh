#pragma once

#include "flux/Scene/Film.h"

namespace flux {
KIRA_DEVICE inline void
Film::DeviceImpl::writeNormal(std::uint32_t x, std::uint32_t y, Vec3f const &value) const noexcept {
    normal[static_cast<std::size_t>(y) * width + x] = value;
}
} // namespace flux
