#include "Scene/Camera.h"

#include <GLFW/glfw3.h>

#include "Scene/Scene.h"

namespace krd {
void CameraController::tick(float deltaTime) {
    // TODO(krr): implement this in the brute force way for now.
    float3 const forward = krd::normalize(camera->getTarget() - camera->getPosition());
    float3 const left = krd::normalize(krd::cross(camera->getUpDirection(), forward));
    float3 const up = krd::normalize(camera->getUpDirection());

    auto moveCameraBy = [&](auto direction) {
        auto target = camera->getTarget();
        auto position = camera->getPosition();

        auto moveVec = deltaTime * moveSpeed * direction;
        camera->setTarget(target + moveVec);
        camera->setPosition(position + moveVec);
    };

    for (auto const &key : keys) {
        switch (key) {
        case GLFW_KEY_UP:
        case GLFW_KEY_W: moveCameraBy(forward); break;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_S: moveCameraBy(-forward); break;

        case GLFW_KEY_A: moveCameraBy(left); break;
        case GLFW_KEY_D: moveCameraBy(-left); break;

        case GLFW_KEY_E: moveCameraBy(up); break;
        case GLFW_KEY_Q: moveCameraBy(-up); break;
        default: LogWarn("CameraController: Unhandled keycode: {:d}", key);
        }
    }
}

void CameraController::onKeyboard(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS)
        keys.insert(key);
    else if (action == GLFW_RELEASE)
        keys.erase(key);
}

Camera::Camera(Scene *scene) : SceneObject(scene), controller(this) {
    getScene()->markActiveCamera(getSceneId());
}

Camera::~Camera() { getScene()->unmarkActiveCamera(getSceneId()); }
} // namespace krd
