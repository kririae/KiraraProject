#include "Instant/InstantCamera.h"

#include <numbers>

#include "Instant/InstantScene.h"
#include "Scene/Camera.h"

namespace krd {
InstantCamera::InstantCamera(InstantScene *iScene) : InstantObject(iScene) {}

bool InstantCamera::pull() {
    auto const camera = getScene()->getActiveCamera().value();

    position = camera->getPosition();
    upDirection = camera->getUpDirection();
    target = camera->getTarget();

    return true;
}

float4x4 InstantCamera::getViewMatrix() const {
    return lookat_matrix(position, target, upDirection);
}

float4x4 InstantCamera::getProjectionMatrix(float aspectRatio) const {
    return perspective_matrix(
        60.0f * std::numbers::pi_v<float> / 180.0f, aspectRatio, 0.1f, 1000.0f
    );
}
} // namespace krd
