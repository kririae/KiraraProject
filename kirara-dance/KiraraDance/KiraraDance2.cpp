#include "Scene/SceneRoot.h"
#include "Scene/Visitors/ExtractTreeHierarchy.h"

int main() {
    using namespace krd;

    auto sceneRoot = SceneRoot::create();
    {
        std::ifstream fs("scene.krd");
        sceneRoot->loadScene(fs);
    }

    ExtractTreeHierarchy tInfo;
    sceneRoot->accept(tInfo);
}
