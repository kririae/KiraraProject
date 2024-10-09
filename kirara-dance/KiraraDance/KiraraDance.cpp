#include "FLux/FLuxScene.h"
#include "FLux/SlangGraphicsContext.h"
#include "Scene/Camera.h"
#include "Scene/Scene.h"
#include "Scene/TriangleMesh.h"

int main() try {
    using namespace krd;

    // Scene: the abstraction of the global state, i.e., configuration (per-program)
    // Scene can be ticked.
    //
    // FLuxScene: the abstraction of render resource creation and manipulation (per-device)
    //
    // SlangGraphicsContext: the abstraction of the graphics API (per-view)
    // No communication in between. SlangGraphicsContext directly responds to FLuxScene's
    // configuration

    // scene > window
    auto const scene = Scene::create(Scene::Desc{});

    scene->create<TriangleMesh>(R"(C:\Users\kriae\Projects\flux-data\cbox\geometry\boxes.ply)");
    scene->create<TriangleMesh>(R"(C:\Users\kriae\Projects\flux-data\cbox\geometry\floor.ply)");
    scene->create<TriangleMesh>(R"(C:\Users\kriae\Projects\flux-data\cbox\geometry\back.ply)");
    scene->create<TriangleMesh>(R"(C:\Users\kriae\Projects\flux-data\cbox\geometry\left.ply)");
    scene->create<TriangleMesh>(R"(C:\Users\kriae\Projects\flux-data\cbox\geometry\right.ply)");

    auto const camera = scene->create<Camera>();
    camera->setPosition(krd::float3(0, 1, 4));
    camera->setTarget(krd::float3(0, 0, 0));
    camera->setUpDirection(krd::float3(0, 1, 0));

    auto const window =
        Window::create(Window::Desc{.width = 1280, .height = 720, .title = "Kirara Dance"});

    // Maybe we should call it \c RenderSystem
    auto const flScene = FLuxScene::create(FLuxScene::Desc{}, scene);

    SlangGraphicsContext::Desc desc{};
    auto const context = SlangGraphicsContext::create(desc, window, flScene);

    //
    window->attachController(camera->getController());
    window->attachController(context->getController());

    // Enter the main loop.
    window->mainLoop([&]() { context->renderFrame(); });

    // Should not be RAII-ed.
    context->finalize();
} catch (std::exception const &e) { krd::LogError("{}", e.what()); }
