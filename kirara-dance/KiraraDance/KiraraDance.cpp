#include "Core/Window.h"
#include "FacadeRender/SlangGraphicsContext.h"
#include "FacadeRender/Visitors/InsertTriMeshResource.h"
#include "Scene/Camera.h"
#include "Scene/SceneBuilder.h"
#include "Scene/SceneRoot.h"
#include "Scene/Visitors/ExtractTreeHierarchy.h"
#include "Scene/Visitors/InsertSkinnedMesh.h"
#include "Scene/Visitors/TickAnimations.h"
#include "Scene/Visitors/ValidateTreeHierarchy.h"

namespace {
class SelectAnimation : public krd::Visitor {
public:
    void apply(krd::Node &val) override { traverse(val); }
    void apply(krd::Animation &val) override { animId = val.getId(); }

public:
    std::optional<uint64_t> getId() const { return animId; }

private:
    std::optional<uint64_t> animId{std::nullopt};
};
} // namespace

int main() try {
    using namespace krd;
    auto const window =
        Window::create(Window::Desc{.width = 1280, .height = 720, .title = "Kirara Dance"});

    SceneBuilder builder;
    builder.loadFromFile(R"(/home/krr/Downloads/CesiumMan.glb)");

    auto const sceneRoot = builder.buildScene();

    // Create the graphics context
    auto SGC = SlangGraphicsContext::create(
        SlangGraphicsContext::Desc{
            .swapchainImageCnt = 2,
            .enableVSync = true,
            .enableGFXFix_07783 = true,
        },
        window
    );

    //
    ValidateTreeHierarchy tChecker;
    sceneRoot->accept(tChecker);
    if (!tChecker.isValidTree())
        throw kira::Anyhow(
            "The traversable scene graph is not a valid tree: {}", tChecker.getDiagnostic()
        );

    auto const camera = Camera::create();
    camera->setPosition(krd::float3(-4, 0, 8));
    camera->setTarget(krd::float3(0, 4, 0));
    camera->setUpDirection(krd::float3(0, 1, 0));
    sceneRoot->getAuxGroup()->addChild(camera);

    //
    ExtractTreeHierarchy tInfo;
    sceneRoot->accept(tInfo);

    //
    window->attachController(camera->getController());
    window->attachController(SGC->getController());

    SelectAnimation sAnim;
    sceneRoot->accept(sAnim);
    if (auto id = sAnim.getId())
        LogInfo("Animation ID {} is selected to display", id.value());
    else
        LogWarn("No animation ID found");

    window->mainLoop([&](float deltaTime) -> void {
#if 1
        bool isNodeUpdated{false};
        if (sAnim.getId()) {
            TickAnimations::Desc desc{.animId = sAnim.getId().value(), .deltaTime = deltaTime};
            TickAnimations tAnim(desc);
            sceneRoot->accept(tAnim);
            if (!tAnim.isMatched())
                LogWarn("No animation ID is not matched");
            else
                isNodeUpdated = true;
        }
#endif

#if 1
        if (isNodeUpdated) {
            InsertSkinnedMesh skinned;
            sceneRoot->accept(skinned);
        }
#endif
        //
        InsertTriMeshResource pResource(SGC.get());
        sceneRoot->accept(pResource);

        SGC->renderFrame(sceneRoot.get(), camera.get());
    });

    return 0;
} catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
