#include "Scene/Scene.h"

#include "Scene/Camera.h"
#include "Scene/TriangleMesh.h"

namespace krd {
Scene::~Scene() {
    // Destruct the objects first, such that the registration map can be corrected updated.
    for (auto &[sObjId, sObj] : sObjMap)
        if (sObj.getRefCount() != 1)
            LogError("Scene: Scene Object id={} outlives the scene", sObjId);
    sObjMap.clear();
    LogTrace("Scene: destructed");
}

void Scene::tick(float deltaTime) {
    for (auto &[sObjId, sObj] : sObjMap)
        sObj->tick(deltaTime);
}

uint64_t Scene::registerSceneObject(Ref<SceneObject> sObj) noexcept {
    auto const sObjId = sObjIdCnt.fetch_add(1) + 1;
    try {
        sObjMap.emplace(sObjId, std::move(sObj));
    } catch (std::exception const &e) {
        LogError("Scene: Failed to register SceneObject: {}", e.what());
        return 0;
    }

    return sObjId;
}

bool Scene::discardSceneObject(uint64_t sObjId) noexcept {
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
