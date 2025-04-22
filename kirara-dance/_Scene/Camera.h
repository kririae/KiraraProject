#pragma once

#include <set>

#include "Core/Math.h"
#include "Core/Window.h"
#include "Scene/SceneObject.h"

namespace krd {
class Camera;

class CameraController final : public Window::Controller {
public:
    explicit CameraController(Camera *camera) : camera(camera) {}

    ///
    void tick(float deltaTime) override;

    ///
    void onKeyboard(int key, int scancode, int action, int mods) override;

private:
    Camera *camera{nullptr};

    ///
    float moveSpeed{1};
    ///
    float rotateSpeed{1};

    ///
    std::set<int> keys;
};

class Camera final : public SceneObject {
    explicit Camera(Scene *scene);

public:
    friend class CameraController;

    ~Camera() override;

    ///
    static Ref<Camera> create(Scene *scene) { return {new Camera(scene)}; }

    ///
    void setPosition(float3 const &pos) { position = pos; }

    ///
    [[nodiscard]] auto getPosition() const { return position; }

    ///
    void setUpDirection(float3 const &up) { upDirection = up; }

    ///
    [[nodiscard]] auto getUpDirection() const { return upDirection; }

    ///
    void setTarget(float3 const &tgt) { target = tgt; }

    ///
    auto getTarget() const { return target; }

    ///
    auto *getController() { return &controller; }

private:
    CameraController controller;

    float3 position{};
    float3 upDirection{0.0f, 1.0f, 0.0f};
    float3 target{};
};
}; // namespace krd
