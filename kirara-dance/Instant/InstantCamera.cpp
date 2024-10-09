#include "Instant/InstantCamera.h"

#include "Instant/InstantScene.h"
#include "Scene/Camera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/matrix_transform.hpp>  // glm::translate, glm::rotate, glm::scale
#include <glm/gtx/string_cast.hpp>       // glm::pi
#include <glm/mat4x4.hpp>                // glm::mat4
#include <glm/vec3.hpp>                  // glm::vec3

namespace krd {
InstantCamera::InstantCamera(InstantScene *iScene) : InstantObject(iScene) {}

bool InstantCamera::pull() {
    auto const camera = getScene()->getActiveCamera().value();

    position = camera->getPosition();
    upDirection = camera->getUpDirection();
    target = camera->getTarget();

    return true;
}

krd::float4x4 InstantCamera::getViewMatrix() const {
    krd::float4x4 viewMat;

    auto const cEye = position;
    auto const cCenter = target;
    auto const cUp = upDirection;
    glm::mat4x4 const view = glm::lookAtRH(
        glm::vec3(cEye.x, cEye.y, cEye.z), glm::vec3(cCenter.x, cCenter.y, cCenter.z),
        glm::vec3(cUp.x, cUp.y, cUp.z)
    );
    std::memcpy(&viewMat, &view, sizeof(view));

    return viewMat;
}

krd::float4x4 InstantCamera::getProjectionMatrix(float aspectRatio) const {
    krd::float4x4 projMat;

    glm::mat4x4 const proj =
        glm::perspectiveRH_ZO(glm::radians(60.0), static_cast<double>(aspectRatio), 0.1, 1000.0);
    std::memcpy(&projMat, &proj, sizeof(proj));

    return projMat;
}
} // namespace krd
