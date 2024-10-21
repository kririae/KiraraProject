#include "Instant/InstantScene.h"
#include "Instant/SlangGraphicsContext.h"
#include "Scene/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneBuilder.h"
#include "Scene/TriangleMesh.h"

int main() try {
    using namespace krd;

    // Scene: the abstraction of the global state, in other words, configuration (per-program)
    // Scene can be ticked.
    //
    // InstantScene: the abstraction of render resource creation and manipulation (per-device)
    // InstantScene shouldn't be ticked.
    //
    // SlangGraphicsContext: the abstraction of the graphics API (per-window)

    SceneBuilder builder{};
    builder.loadFromFile(R"(C:\Users\kriae\Downloads\Compressed\glTF-Sample-Assets-main_4\glTF-Sample-Assets-main\Models\Duck\glTF\Duck.gltf)");

    auto const scene = builder.buildScene();

    auto const camera = scene->create<Camera>();
    camera->setPosition(krd::float3(0, 1, 4));
    camera->setTarget(krd::float3(0, 0, 0));
    camera->setUpDirection(krd::float3(0, 1, 0));

    auto const instScene = InstantScene::create(InstantScene::Desc{}, scene);
    auto const window =
        Window::create(Window::Desc{.width = 1280, .height = 720, .title = "Kirara Dance"});

    SlangGraphicsContext::Desc desc{};
    auto const context = SlangGraphicsContext::create(desc, window, instScene);

    //
    window->attachController(camera->getController());
    window->attachController(context->getController());

    // Enter the main loop.
    window->mainLoop([&](float deltaTime) {
        scene->tick(deltaTime);

        instScene->pull();
        context->renderFrame();
    });

    // Shouldn't be RAII-ed.
    context->synchronize();
} catch (std::exception const &e) {
    //
    krd::LogError("{}", e.what());
}
