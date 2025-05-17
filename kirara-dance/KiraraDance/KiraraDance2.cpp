#include "Scene/Scene.h"
#include "Scene/Visitors/ExtractTreeHierarchy.h"

int main() {
    using namespace krd;

    std::ifstream fs("scene.krd");
    SerializationContext ctx;

    std::string str;
    cereal::BinaryInputArchive ar(fs);
    ar(ctx);
    ar(str);

    std::stringstream ss(std::move(str));
    auto sceneRoot = SceneRoot::create();
    sceneRoot->fromBytes(ctx, ss);

    ExtractTreeHierarchy tInfo;
    sceneRoot->accept(tInfo);
}
