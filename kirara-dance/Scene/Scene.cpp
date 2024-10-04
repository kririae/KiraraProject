#include "Scene/Scene.h"

#include "Scene/TriangleMesh.h"

namespace krd {
uint64_t Scene::registerSceneObject(Ref<SceneObjectBase> sceneObj) {
    auto const sObjId = sObjIdCnt.fetch_add(1) + 1;
    sObjMap.emplace(sObjId, std::move(sceneObj));
    return sObjId;
}

kira::SmallVector<Ref<TriangleMesh>> Scene::getTriangleMesh() const {
    return getSceneObjectOfType<TriangleMesh>();
}
} // namespace krd
