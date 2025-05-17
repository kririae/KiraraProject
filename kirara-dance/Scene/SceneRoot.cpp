#include "SceneRoot.h"

namespace krd {
void SceneRoot::dumpScene(std::ostream &os) {
    std::stringstream ss;
    SerializationContext ctx;
    this->toBytes(ctx, ss);

    cereal::BinaryOutputArchive ar(os);
    // in general, ss will be quite small
    ar(ss.str());
    ar(ctx);
}

void SceneRoot::loadScene(std::istream &is) {
    std::string str;
    SerializationContext ctx;
    cereal::BinaryInputArchive ar(is);
    ar(str);
    ar(ctx);

    std::stringstream ss{std::move(str)};
    this->fromBytes(ctx, ss);
}
} // namespace krd
