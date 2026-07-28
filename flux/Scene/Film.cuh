#pragma once

#include "flux/Scene/Film.h"

namespace flux {
KIRA_DEVICE inline void
Film::DeviceImpl::accumulateNormal(Vec2u const &pixel, Vec3f const &value) const noexcept {
    auto &destination = normal[static_cast<std::size_t>(pixel.y()) * width + pixel.x()];
    atomicAdd(&destination[0], value[0]);
    atomicAdd(&destination[1], value[1]);
    atomicAdd(&destination[2], value[2]);
}
} // namespace flux
