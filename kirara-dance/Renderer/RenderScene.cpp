#include "Renderer/RenderScene.h"

#include "Renderer/RenderTriangleMesh.h"

namespace krd {
RenderScene::RenderScene(Ref<Scene> scene) : scene(std::move(scene)) {
    // `scene` should only be accessed through `getScene()` from now.
    this->doInit(getScene());
}

uint64_t RenderScene::registerRenderObject(Ref<RenderObject> renderObj) {
    auto const renderObjId = renderObjIdCnt.fetch_add(1) + 1;
    renderObjMap.emplace(renderObjId, std::move(renderObj));

    return renderObjId;
}

void RenderScene::doInit(Ref<Scene> const &scene) { initStaticTriangleMeshes(); }

void RenderScene::initStaticTriangleMeshes() {
    for (auto const &staticTriangleMesh : getScene()->getStaticTriangleMeshRange())
        auto rObj = this->create<RenderStaticTriangleMesh>(staticTriangleMesh);
}
} // namespace krd
