#pragma once

#include "Core/Object.h"

namespace krd {
class Scene;

/// Base class for an object that belongs to a scene.
class SceneObject : public Object {
public:
    ~SceneObject() override = default;

    /// Gets the scene that this object belongs to.
    [[nodiscard]] Scene *getScene() const { return scene; }

protected:
    SceneObject(Scene *scene) : scene(scene) {}

    /// The scene that this object belongs to.
    Scene *scene{nullptr};

    /// The unique identifier of this object in the scene.
    uint32_t sceneId{0};
};
} // namespace krd
