#include "flux/Scene/Camera.h"

#include <cmath>
#include <numbers>

#include "flux/Core/MathUtils.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
void validateVerticalFieldOfView(float degrees) {
    if (!(degrees > 0.0F && degrees < 180.0F))
        throw kira::Anyhow("Camera: vertical field of view must be between 0 and 180 degrees");
}

void validateThinLens(float radius, float focalDistance) {
    if (!(radius >= 0.0F && std::isfinite(radius)))
        throw kira::Anyhow("Camera: lens radius must be finite and nonnegative");
    if (!(focalDistance >= 0.0F && std::isfinite(focalDistance)))
        throw kira::Anyhow("Camera: focal distance must be finite and nonnegative");
    if (radius > 0.0F && !(focalDistance > 0.0F))
        throw kira::Anyhow("Camera: a positive lens radius requires a positive focal distance");
}
} // namespace

Ref<Camera> Camera::create(kira::Properties const &props) { return Ref<Camera>{new Camera(props)}; }

Camera::Camera(kira::Properties const &props) {
    position_ = props.use_or<Vec3f>("position", position_);
    lookAt_ = props.use_or<Vec3f>("look_at", lookAt_);
    referenceUp_ = props.use_or<Vec3f>("ref_up", referenceUp_);
    verticalFieldOfView_ = props.use_or<float>("fov", verticalFieldOfView_);
    lensRadius_ = props.use_or<float>("lens_radius", lensRadius_);
    focalDistance_ = props.use_or<float>("focal_distance", focalDistance_);
    validateVerticalFieldOfView(verticalFieldOfView_);
    validateThinLens(lensRadius_, focalDistance_);
}

void Camera::setVerticalFieldOfView(float degrees) {
    validateVerticalFieldOfView(degrees);
    verticalFieldOfView_ = degrees;
}

void Camera::setLensRadius(float radius) {
    validateThinLens(radius, focalDistance_);
    lensRadius_ = radius;
}

void Camera::setFocalDistance(float distance) {
    validateThinLens(lensRadius_, distance);
    focalDistance_ = distance;
}

Camera::Impl Camera::getImpl() const {
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
        .lensRadius = lensRadius_,
        .focalDistance = focalDistance_,
    };
}
} // namespace flux
