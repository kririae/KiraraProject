#include "Core/Window.h"
#include "FacadeRender/SlangGraphicsContext.h"
#include "FacadeRender/Visitors/InsertTriMeshResource.h"
#include "Scene/Camera.h"
#include "Scene/SceneRoot.h"
#include "Scene/Visitors/ExtractTreeHierarchy.h"
#include "Scene/Visitors/InsertSkinnedMesh.h"
#include "Scene/Visitors/TickAnimations.h"
#include "Scene/Visitors/ValidateTreeHierarchy.h"
#include "SceneGraph/Visitors/ExtractTypeOf.h"

int main() {
    using namespace krd;
    auto const window =
        Window::create(Window::Desc{.width = 720, .height = 1280, .title = "Kirara Dance"});

    auto sceneRoot = SceneRoot::create();
    {
        std::ifstream fs("scene.krd");
        sceneRoot->loadScene(fs);
    }

    ExtractTreeHierarchy tInfo;
    sceneRoot->accept(tInfo);

    // Create the graphics context
    auto SGC = SlangGraphicsContext::create(
        SlangGraphicsContext::Desc{
            .swapchainImageCnt = 3,
            .enableVSync = true,
            .enableGFXFix_07783 = false,
        },
        window
    );

    // window->attachController(camera->getController());
    window->attachController(SGC->getController());

    ExtractTypeOf<Animation> extAnim;
    sceneRoot->accept(extAnim);
    if (extAnim.empty())
        throw kira::Anyhow("No animation found in the scene");
    LogInfo("Animation ID {} is selected to display", extAnim.front()->getId());

    ExtractTypeOf<Camera> extCam;
    sceneRoot->accept(extCam);
    if (extCam.empty())
        throw kira::Anyhow("No camera found in the scene");

    window->mainLoop([&](float deltaTime) -> void {
        // This is currently a hack, to discard any modifications to the sceneRoot itself, i.e.,
        // additional resource attachments.
        auto transientSceneRoot = sceneRoot->clone();
        KRD_ASSERT(transientSceneRoot);

        bool isNodeUpdated{false};
        if (!extAnim.empty()) {
            TickAnimations::Desc desc{.animId = extAnim.front()->getId(), .deltaTime = deltaTime};
            TickAnimations tAnim(desc);
            transientSceneRoot->accept(tAnim);
            if (!tAnim.isMatched())
                LogWarn("No animation ID is not matched");
            else
                isNodeUpdated = true;
        }

#if 1
        InsertSkinnedMesh skinned({});
        transientSceneRoot->accept(skinned);
#endif

        InsertTriMeshResource pResource(SGC);
        transientSceneRoot->accept(pResource);

        //
        ValidateTreeHierarchy tChecker;
        transientSceneRoot->accept(tChecker);
        if (!tChecker.isValidTree())
            throw kira::Anyhow(
                "The traversable scene graph is not a valid tree: {}", tChecker.getDiagnostic()
            );
        SGC->renderFrame(transientSceneRoot, extCam.front());
    });

    return 0;
}

#if 0
catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
#endif
