#include <exception>

#include "Renderer/RenderScene.h"
#include "Renderer/SlangGraphicsContext.h"
#include "Scene/Scene.h"
#include "Scene/TriangleMesh.h"

int main() try {
    using namespace krd;

    auto scene = Scene::create();
    scene->create<TriangleMesh>("/home/krr/Projects/flux/flux/data/cbox/geometry/back.ply");

    auto renderScene = RenderScene::create(scene);
    auto window = Window::create(Window::Desc{800, 600, "Kirara Dance"});

    SlangGraphicsContext::Desc desc{.enableGFXFix_07783 = true};
    auto context = SlangGraphicsContext::create(desc, window);

#if 0
    while (true)
        context->renderFrame();
#endif
} catch (std::exception const &e) { krd::LogError("{}", e.what()); }
