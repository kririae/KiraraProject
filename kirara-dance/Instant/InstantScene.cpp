#include "Instant/InstantScene.h"

#include "Instant/InstantCamera.h"
#include "Instant/InstantTriangleMesh.h"
#include "Instant/SlangGraphicsContext.h"
#include "Scene/Camera.h"

namespace krd {
InstantScene::InstantScene(Desc const &desc, Ref<Scene> scene) : scene(std::move(scene)) {
    // `scene` should only be accessed through `getScene()` from now on.
    (void)(desc);
    this->doInit(getScene());

    // Pull the data from the scene objects.
    this->pull();
}

InstantScene::~InstantScene() { LogTrace("InstantScene: destructed"); }

uint64_t InstantScene::registerInstantObject(Ref<InstantObject> rObj) {
    auto const rObjId = iObjIdCnt.fetch_add(1) + 1;
    iObjMap.emplace(rObjId, std::move(rObj));
    return rObjId;
}

Ref<InstantCamera> InstantScene::getActiveCamera() const {
    KRD_ASSERT(activeCameraId);
    return getAs<InstantCamera>(activeCameraId);
}

void InstantScene::pull() {
    for (auto const &[rObjId, rObj] : iObjMap) {
        auto succeed = rObj->pull();
        (void)(succeed);
    }
}

ProgramBuilder InstantScene::createProgramBuilder() const {
    // Return an instance of ProgramBuilder to avoid maintaining state.
    std::filesystem::path const shadersPath = R"(InstantScene/VFMain.slang)";

    ProgramBuilder programBuilder;
    programBuilder.addSlangModuleFromPath(shadersPath)
        .addEntryPoint("vertexMain")
        .addEntryPoint("fragmentMain");
    return programBuilder;
}

void InstantScene::doInit(Ref<Scene> const &scene) {
    (void)(scene);
    initTriangleMeshes();
    initCameras();
}

void InstantScene::initTriangleMeshes() {
    for (auto const &staticTriangleMesh : getScene()->getTriangleMesh())
        auto rObj = this->create<InstantTriangleMesh>(staticTriangleMesh);
}

void InstantScene::initCameras() {
    if (getScene()->getActiveCamera().has_value())
        activeCameraId = this->create<InstantCamera>()->getInstantSceneId();
    else
        throw kira::Anyhow("InstantScene: No active camera in the scene");
}
} // namespace krd
