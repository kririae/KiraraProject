#pragma once

#include <numbers>
#include <set>

#include "Core/Math.h"
#include "Core/Window.h"
#include "SceneGraph/Node.h"
#include "SceneGraph/NodeMixins.h"
#include "SceneGraph/Serialization.h"

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
    float moveSpeed{100};
    ///
    float rotateSpeed{40};

    ///
    std::set<int> keys;
};

class Camera final : public SerializableMixin<Camera, Node, "krd::Camera"> {
    Camera() : controller(this) {}

public:
    friend class CameraController;
    ~Camera() override = default;

    ///
    [[nodiscard]] static Ref<Camera> create() { return {new Camera}; }

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
    [[nodiscard]] auto getTarget() const { return target; }
    ///
    [[nodiscard]] auto *getController() { return &controller; }

public:
    ///
    [[nodiscard]] float4x4 getViewMatrix() const {
        return lookat_matrix(position, target, upDirection);
    }

    ///
    [[nodiscard]] float4x4 getProjectionMatrix(float aspectRatio) const {
        // TODO(krr): change this to user-specified numbers
        return perspective_matrix(
            60.0f * std::numbers::pi_v<float> / 180.0f, aspectRatio, 0.1f, 1000.0f
        );
    }

private:
    CameraController controller;

    float3 position{};
    float3 upDirection{0.0f, 1.0f, 0.0f};
    float3 target{};

public:
    void archive(auto &ar) {
        ar(cereal::make_nvp("position", position),       //
           cereal::make_nvp("upDirection", upDirection), //
           cereal::make_nvp("target", target));
    }
};
}; // namespace krd
