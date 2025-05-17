#include "Scene/Scene.h"
#include "Scene/Visitors/ExtractTreeHierarchy.h"

int main() {
    using namespace krd;

    std::ifstream fs("scene.krd");
    SerializationContext ctx;

    std::string ss;
    cereal::BinaryInputArchive ar(fs);
    ar(ctx);
    ar(ss);

    std::stringstream ss2(std::move(ss));
    auto sceneRoot = SceneRoot::create();
    sceneRoot->fromBytes(ctx, ss2);

    ExtractTreeHierarchy tInfo;
    sceneRoot->accept(tInfo);
}
