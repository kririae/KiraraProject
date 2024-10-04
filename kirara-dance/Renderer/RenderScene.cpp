#include "Renderer/RenderScene.h"

#include "Renderer/RenderTriangleMesh.h"
#include "Renderer/SlangGraphicsContext.h"

namespace krd {
RenderScene::RenderScene(Desc const &desc, Ref<Scene> scene) : scene(std::move(scene)) {
    // `scene` should only be accessed through `getScene()` from now on.
    (void)(desc);
    this->doInit(getScene());

    // Pull the data from the scene objects.
    this->pull();
}

uint64_t RenderScene::registerRenderObject(Ref<RenderObject> rObj) {
    auto const rObjId = rObjIdCnt.fetch_add(1) + 1;
    rObjMap.emplace(rObjId, std::move(rObj));
    return rObjId;
}

void RenderScene::bindGraphicsContext(Ref<SlangGraphicsContext> ctx) {
    KRD_ASSERT(ctx);
    KRD_ASSERT(!this->ctx);
    this->ctx = std::move(ctx);
}

void RenderScene::pull() {
    for (auto const &[rObjId, rObj] : rObjMap) {
        auto succeed = rObj->pull();
        (void)(succeed);
    }
}

ProgramBuilder RenderScene::createProgramBuilder() const {
    // Return a instance of ProgramBuilder to avoid maintaining state.
    std::filesystem::path const shadersPath =
        "/home/krr/Projects/KiraraProject/kirara-dance/Renderer/VFMain.slang";

    ProgramBuilder programBuilder;
    programBuilder.addSlangModuleFromPath(shadersPath)
        .addEntryPoint("vertexMain")
        .addEntryPoint("fragmentMain");
    return programBuilder;
}

void RenderScene::doInit(Ref<Scene> const &scene) { initTriangleMeshes(); }

void RenderScene::initTriangleMeshes() {
    for (auto const &staticTriangleMesh : getScene()->getTriangleMesh())
        auto rObj = this->create<RenderTriangleMesh>(staticTriangleMesh);
}
} // namespace krd
