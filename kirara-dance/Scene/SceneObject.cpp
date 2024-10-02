#include "Scene/SceneObject.h"

#include "Scene/Scene.h"

namespace krd {
SceneObjectBase::SceneObjectBase(Scene *scene)
    : scene(scene), sceneId(scene->registerSceneObject(Ref<SceneObjectBase>(this))) {}

void SceneObjectBase::markRenderable() const {
    KRD_ASSERT(getScene() != nullptr);
    KRD_ASSERT(getSceneId() != 0);
    getScene()->markRenderable(getSceneId());
}

void SceneObjectBase::markPhysical() const {
    KRD_ASSERT(getScene() != nullptr);
    KRD_ASSERT(getSceneId() != 0);
    getScene()->markPhysical(getSceneId());
}
} // namespace krd
