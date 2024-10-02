#include "Scene/Scene.h"

namespace krd {
uint64_t Scene::registerSceneObject(Ref<SceneObjectBase> sceneObj) {
    auto const sceneObjId = sceneObjIdCnt.fetch_add(1) + 1;
    sceneObjMap.emplace(sceneObjId, std::move(sceneObj));

    return sceneObjId;
}

void Scene::markStaticTriangleMesh(uint64_t sceneObjId) {
    staticTriangleMeshSceneObjIds.insert(sceneObjIdCnt);
}
} // namespace krd
