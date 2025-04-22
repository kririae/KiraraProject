#include "Core/Window.h"
#include "Scene2/SceneBuilder.h"
#include "Scene2/SceneRoot.h"
#include "Scene2/Visitors/PrintStatsVisitor.hpp"

int main() try {
    using namespace krd;
    auto const window =
        Window::create(Window::Desc{.width = 1280, .height = 720, .title = "Kirara Dance"});

    SceneBuilder builder;
    builder.loadFromFile(R"(/home/krr/Downloads/Duck.glb)");

    auto const sceneRoot = builder.buildScene();
    PrintStatsVisitor psVisitor;
    sceneRoot->accept(psVisitor);
    return 0;
} catch (std::exception const &e) {
    krd::LogError("{}", e.what());
    return -1;
}
