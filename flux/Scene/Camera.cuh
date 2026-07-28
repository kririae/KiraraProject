#pragma once

#include "flux/Core/MathUtils.h"
#include "flux/Scene/Camera.h"

namespace flux {
KIRA_DEVICE inline Ray Camera::DeviceImpl::generateRay(
    Vec2f const &rasterPosition, Vec2f const &lensSample, Vec2u const &resolution
) const noexcept {
    auto const inverseWidth = 1.0F / static_cast<float>(resolution.x());
    auto const inverseHeight = 1.0F / static_cast<float>(resolution.y());
    auto const screenX = rasterPosition.x() * inverseWidth * 2.0F - 1.0F;
    auto const screenY = 1.0F - rasterPosition.y() * inverseHeight * 2.0F;
    auto const aspect = static_cast<float>(resolution.x()) * inverseHeight;
    auto const unnormalizedDirection =
        forward + right * (screenX * halfHeight * aspect) + up * (screenY * halfHeight);
    auto const directionLength = unnormalizedDirection.norm();
    auto const direction = unnormalizedDirection / directionLength;

    if (lensRadius == 0.0F)
        return {
            .origin = position,
            .direction = direction,
        };

    auto const diskSample = uniformSampleDisk(lensSample) * lensRadius;
    auto const lensPosition = position + right * diskSample.x() + up * diskSample.y();
    auto const focusPoint = position + direction * (focalDistance * directionLength);

    return {
        .origin = lensPosition,
        .direction = (focusPoint - lensPosition).normalize(),
    };
}
} // namespace flux
