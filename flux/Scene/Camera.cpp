#include "flux/Scene/Camera.h"

#include <cmath>
#include <numbers>
#include <utility>

#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] Vec3f cross(Vec3f const &lhs, Vec3f const &rhs) noexcept {
    return {
        lhs.y() * rhs.z() - lhs.z() * rhs.y(),
        lhs.z() * rhs.x() - lhs.x() * rhs.z(),
        lhs.x() * rhs.y() - lhs.y() * rhs.x(),
    };
}

} // namespace

Camera::Camera(TXContext &tx, kira::Properties properties)
    : RenderObject(tx, std::move(properties)) {
    auto const &storedProperties = getProperties();
    position_ = storedProperties.use_or<Vec3f>("position", position_);
    lookAt_ = storedProperties.use_or<Vec3f>("look_at", lookAt_);
    referenceUp_ = storedProperties.use_or<Vec3f>("ref_up", referenceUp_);
    setVerticalFieldOfView(storedProperties.use_or<float>("fov", verticalFieldOfView_));
}

void Camera::setVerticalFieldOfView(float degrees) {
    if (!(degrees > 0.0F && degrees < 180.0F))
        throw kira::Anyhow("Camera: vertical field of view must be between 0 and 180 degrees");
    verticalFieldOfView_ = degrees;
}

Camera::DeviceImpl Camera::getDeviceImpl() const {
    auto const view = lookAt_ - position_;
    auto const viewLength = view.norm();
    if (!(viewLength > 0.0F && std::isfinite(viewLength)))
        throw kira::Anyhow("Camera: viewing direction must be finite and nonzero");
    Vec3f const forward = view / viewLength;
    auto const horizontal = cross(forward, referenceUp_);
    auto const horizontalLength = horizontal.norm();
    if (!(horizontalLength > 0.0F && std::isfinite(horizontalLength)))
        throw kira::Anyhow(
            "Camera: reference up must be finite and not parallel to the viewing direction"
        );
    Vec3f const right = horizontal / horizontalLength;
    auto const up = cross(right, forward);
    auto const halfHeight = std::tan(verticalFieldOfView_ * std::numbers::pi_v<float> / 360.0F);

    return {
        .position = position_,
        .forward = forward,
        .right = right,
        .up = up,
        .halfHeight = halfHeight,
    };
}
} // namespace flux
