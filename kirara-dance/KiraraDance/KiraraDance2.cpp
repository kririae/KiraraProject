#include "Core/Window.h"
#include "ImmediateRender/PopulateResource.h"
#include "ImmediateRender/SlangGraphicsContext.h"
#include "Scene2/Camera.h"
#include "Scene2/SceneBuilder.h"
#include "Scene2/SceneRoot.h"
#include "Scene2/TreeChecker.h"
#include "Scene2/TreeInfo.h"

int main() try {
    using namespace krd;
    auto const window =
        Window::create(Window::Desc{.width = 1280, .height = 720, .title = "Kirara Dance"});

    SceneBuilder builder;
    builder.loadFromFile(R"(/Users/krr/Documents/Projects/KiraraProject/Duck.glb)");

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

    // (1)
    TreeChecker tChecker;
    sceneRoot->accept(tChecker);
    if (!tChecker.isValidTree())
        throw kira::Anyhow(
            "The traversable scene graph is not a valid tree: {}", tChecker.getDiagnostic()
        );

    // (2)
    PopulateResource pResource(SGC.get());
    sceneRoot->accept(pResource);

    // (3)
    TreeInfo tInfo;
    sceneRoot->accept(tInfo);

    auto const camera = Camera::create();
    camera->setPosition(krd::float3(0, 1, 4));
    camera->setTarget(krd::float3(0, 0, 0));
    camera->setUpDirection(krd::float3(0, 1, 0));

    //
    window->attachController(camera->getController());
    window->attachController(SGC->getController());
    window->mainLoop([&](float deltaTime) -> void {
        (void)(deltaTime);
        //
        SGC->renderFrame(sceneRoot.get(), camera.get());
    });

    return 0;
} catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
