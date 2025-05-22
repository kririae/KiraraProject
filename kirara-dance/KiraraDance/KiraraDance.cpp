#include "Core/Math.h"
#include "Core/Window.h"
#include "FacadeRender/SlangGraphicsContext.h"
#include "FacadeRender/Visitors/InsertTriMeshResource.h"
#include "Scene/Camera.h"
#include "Scene/Geometry.h"
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
        Window::create(Window::Desc{.width = 720, .height = 1280, .title = "Kirara Dance"});

    SceneBuilder builder;
    builder.loadFromFile(R"(/home/krr/Downloads/RiggedSimple.glb)");

    auto const sceneRoot = builder.buildScene();

#if 1
    // Create the graphics context
    auto SGC = SlangGraphicsContext::create(
        SlangGraphicsContext::Desc{
            .swapchainImageCnt = 3,
            .enableVSync = true,
            .enableGFXFix_07783 = false,
        },
        window
    );
#endif

    auto const camera = Camera::create();
    camera->setPosition(krd::float3(-60, 60, 120));
    camera->setTarget(krd::float3(0, 60, 0));
    camera->setUpDirection(krd::float3(0, 1, 0));
    sceneRoot->getAuxGroup()->addChild(camera);

    //
    std::stringstream ss;
    ExtractTreeHierarchy tInfo(ExtractTreeHierarchy::Descriptor{.os = ss});
    sceneRoot->accept(tInfo);
    LogInfo("Scene hierarchy:\n{}", std::move(ss).str());

    {
        std::ofstream fs;
        fs.open("scene.krd", std::ios::out | std::ios::binary);
        sceneRoot->dumpScene(fs);
    }

    //
    window->attachController(camera->getController());
    window->attachController(SGC->getController());

    SelectAnimation sAnim;
    sceneRoot->accept(sAnim);
    if (auto id = sAnim.getId())
        LogInfo("Animation ID {} is selected to display", id.value());
    else
        LogWarn("No animation ID found");

#if 1
    window->mainLoop([&](float deltaTime) -> void {
        // This is currently a hack, to discard any modifications to the sceneRoot itself, i.e.,
        // additional resource attachments.
        auto transientSceneRoot = sceneRoot->clone();
        KRD_ASSERT(transientSceneRoot);

        bool isNodeUpdated{false};
        if (sAnim.getId()) {
            TickAnimations::Desc desc{.animId = sAnim.getId().value(), .deltaTime = deltaTime};
            TickAnimations tAnim(desc);
            transientSceneRoot->accept(tAnim);
            if (!tAnim.isMatched())
                LogWarn("No animation ID is not matched");
            else
                isNodeUpdated = true;
        }

        InsertSkinnedMesh skinned({});
        transientSceneRoot->accept(skinned);

        InsertTriMeshResource pResource(SGC);
        transientSceneRoot->accept(pResource);

        //
        ValidateTreeHierarchy tChecker;
        transientSceneRoot->accept(tChecker);
        if (!tChecker.isValidTree())
            throw kira::Anyhow(
                "The traversable scene graph is not a valid tree: {}", tChecker.getDiagnostic()
            );
        SGC->renderFrame(transientSceneRoot, camera);
    });
#endif

    return 0;
} catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
