#include "Core/Window.h"
#include "Scene2/SceneBuilder.h"
#include "Scene2/SceneRoot.h"
#include "Scene2/Visitors/TreeInfo.hpp"

int main() try {
    using namespace krd;
    auto const window =
        Window::create(Window::Desc{.width = 1280, .height = 720, .title = "Kirara Dance"});

    SceneBuilder builder;
    builder.loadFromFile(R"(/Users/krr/Downloads/Duck.glb)");

    auto const sceneRoot = builder.buildScene();
    TreeInfo tVisitor;
    sceneRoot->accept(tVisitor);
    return 0;
} catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
