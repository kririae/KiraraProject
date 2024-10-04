#include <exception>

#include "Renderer/RenderScene.h"
#include "Renderer/SlangGraphicsContext.h"
#include "Scene/Scene.h"
#include "Scene/TriangleMesh.h"

int main() try {
    using namespace krd;

    //! It is quite stupid, but \c Window \em must be created before everything else(and thus be
    //! destructed the last), otherwise slang will crash, because in destructing \c gfx::ISwapchain,
    //! the window handle is used.
    auto window =
        Window::create(Window::Desc{.width = 800, .height = 600, .title = "Kirara Dance"});

    auto scene = Scene::create(Scene::Desc{});
    scene->create<TriangleMesh>("/home/krr/Projects/flux/flux/data/cbox/geometry/back.ply");

    // Maybe we should call it \c RenderSystem
    // scene <- renderScene -> SlangGraphicsContext
    auto renderScene = RenderScene::create(RenderScene::Desc{}, scene);

    SlangGraphicsContext::Desc desc{.enableGFXFix_07783 = true};
    auto context = SlangGraphicsContext::create(desc, window, renderScene);

    while (true)
        context->renderFrame();
} catch (std::exception const &e) { krd::LogError("{}", e.what()); }

