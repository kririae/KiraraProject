#include "Scene/Scene.h"

#include "Scene/Camera.h"
#include "Scene/TriangleMesh.h"

namespace krd {
Scene::~Scene() {
    for (auto &[sObjId, sObj] : sObjMap)
        if (sObj.getRefCount() != 1)
            LogError("Scene: Scene Object {} outlives the scene", sObjId);
    sObjMap.clear();
}

uint64_t Scene::registerSceneObject(Ref<SceneObjectBase> sObj) {
    auto const sObjId = sObjIdCnt.fetch_add(1) + 1;
    sObjMap.emplace(sObjId, std::move(sObj));
    return sObjId;
}

bool Scene::discardSceneObject(uint64_t sObjId) {
    auto const it = sObjMap.find(sObjId);
    if (it == sObjMap.end())
        return false;
    sObjMap.erase(it);
    return true;
}

std::optional<Ref<Camera>> Scene::getActiveCamera() const {
    if (activeCameraId == 0)
        return std::nullopt;
    return getAs<Camera>(activeCameraId);
}

kira::SmallVector<Ref<TriangleMesh>> Scene::getTriangleMesh() const {
    return getSceneObjectOfType<TriangleMesh>();
}
} // namespace krd
