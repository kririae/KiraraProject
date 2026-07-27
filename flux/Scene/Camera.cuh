#pragma once

#include "flux/Scene/Camera.h"

namespace flux {
KIRA_DEVICE inline Ray Camera::DeviceImpl::generateRay(
    Vec2f const &rasterPosition, std::uint32_t width, std::uint32_t height
) const noexcept {
    auto const inverseWidth = 1.0F / static_cast<float>(width);
    auto const inverseHeight = 1.0F / static_cast<float>(height);
    auto const screenX = rasterPosition.x() * inverseWidth * 2.0F - 1.0F;
    auto const screenY = 1.0F - rasterPosition.y() * inverseHeight * 2.0F;
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
