#pragma once

#include "flux/Scene/Camera.h"

namespace flux {
KIRA_DEVICE inline Ray Camera::DeviceImpl::generateRay(
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height
) const noexcept {
    auto const inverseWidth = 1.0F / static_cast<float>(width);
    auto const inverseHeight = 1.0F / static_cast<float>(height);
    auto const screenX = (static_cast<float>(x) + 0.5F) * inverseWidth * 2.0F - 1.0F;
    auto const screenY = 1.0F - (static_cast<float>(y) + 0.5F) * inverseHeight * 2.0F;
    auto const aspect = static_cast<float>(width) * inverseHeight;
    auto const direction =
        (forward + right * (screenX * halfHeight * aspect) + up * (screenY * halfHeight))
            .normalize();

    return {
        .origin = position,
        .direction = direction,
    };
}
} // namespace flux
