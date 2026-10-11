#include "flux/Scene/Camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "flux/Core/Logging.h"
#include "flux/Core/MathUtils.h"
#include "flux/Shading/Frame.h"

namespace flux {
namespace {
constexpr float minVerticalFieldOfView = 0.001F;
constexpr float maxVerticalFieldOfView = 179.999F;

/// Returns \p vector scaled to unit length, or nothing if its length is zero.
[[nodiscard]] std::optional<Vec3f> tryNormalize(Vec3f const &vector) {
    auto const length = vector.norm();
    if (!(length > 0.0F))
        return std::nullopt;
    return vector / length;
}

/// Returns \p value if it is nonnegative, otherwise reports it and returns 0.
[[nodiscard]] float boundLensValue(float value, char const *name) {
    if (value >= 0.0F)
        return value;
    LogWarn("Camera: {} {} is invalid, using 0", name, value);
    return 0.0F;
}
} // namespace

Ref<Camera> Camera::create(kira::Properties const &props) { return Ref<Camera>{new Camera(props)}; }

Camera::Camera(kira::Properties const &props) {
    position_ = props.use_or<Vec3f>("position", position_);
    lookAt_ = props.use_or<Vec3f>("look_at", lookAt_);
    referenceUp_ = props.use_or<Vec3f>("ref_up", referenceUp_);
    setVerticalFieldOfView(props.use_or<float>("fov", verticalFieldOfView_));
    setLensRadius(props.use_or<float>("lens_radius", lensRadius_));
    setFocalDistance(props.use_or<float>("focal_distance", focalDistance_));
    if (lensRadius_ > 0.0F && !(focalDistance_ > 0.0F))
        LogWarn("Camera: lens radius is positive but focal distance is not, using a pinhole");
}

void Camera::setVerticalFieldOfView(float degrees) noexcept {
    verticalFieldOfView_ = std::clamp(degrees, minVerticalFieldOfView, maxVerticalFieldOfView);
}

void Camera::setLensRadius(float radius) { lensRadius_ = boundLensValue(radius, "lens radius"); }

void Camera::setFocalDistance(float distance) {
    focalDistance_ = boundLensValue(distance, "focal distance");
}

Camera::Impl Camera::getImpl() const noexcept {
    // A zero view vector leaves no direction to keep, so the camera looks along -Z with +Y up.
    Vec3f forward{0.0F, 0.0F, -1.0F};
    Vec3f right{1.0F, 0.0F, 0.0F};
    Vec3f up{0.0F, 1.0F, 0.0F};
    if (auto const view = tryNormalize(lookAt_ - position_)) {
        forward = *view;
        if (auto const horizontal = tryNormalize(cross(forward, referenceUp_))) {
            right = *horizontal;
            up = cross(right, forward);
        } else {
            // A zero or parallel reference up leaves the roll free, so any perpendicular axes do.
            Frame const frame(-forward);
            right = frame.toWorld({1.0F, 0.0F, 0.0F});
            up = frame.toWorld({0.0F, 1.0F, 0.0F});
        }
    }
    auto const halfHeight = std::tan(verticalFieldOfView_ * std::numbers::pi_v<float> / 360.0F);
    auto const hasLens = lensRadius_ > 0.0F && focalDistance_ > 0.0F;

    return {
        .position = position_,
        .forward = forward,
        .right = right,
        .up = up,
        .halfHeight = halfHeight,
        .lensRadius = hasLens ? lensRadius_ : 0.0F,
        .focalDistance = hasLens ? focalDistance_ : 0.0F,
    };
}
} // namespace flux
