#include "Scene/SceneObject.h"

#include "Scene/Scene.h"

namespace krd {
SceneObject::SceneObject(Scene *scene) noexcept
    : scene(scene), sceneId(scene->registerSceneObject(this)) {}
} // namespace krd
