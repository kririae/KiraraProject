#include "Core/Window.h"
#include "ImmediateRender/IssueDrawCommand.h"
#include "ImmediateRender/PopulateResource.h"
#include "ImmediateRender/SlangGraphicsContext.h"
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
    {
        TreeChecker tChecker;
        sceneRoot->accept(tChecker);
        if (!tChecker.isValidTree())
            throw kira::Anyhow(
                "The traversable scene graph is not a valid tree: {}", tChecker.getDiagnostic()
            );
    }

    // (2)
    {
        PopulateResource pResource(SGC.get());
        sceneRoot->accept(pResource);
    }

    // (3)
    {
        TreeInfo tInfo;
        sceneRoot->accept(tInfo);
    }

    return 0;
} catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
