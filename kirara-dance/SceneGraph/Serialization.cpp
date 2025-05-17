#include "Serialization.h"

namespace krd {
bool SerializableFactory::registerNodeCreator(
    std::size_t typeHash, std::function<Ref<Node>()> creator
) {
    // NOTE(krr): no exception or assertion here. In the context of invoking the
    // registerNodeCreator, the exception handling context might not be fully set up.
    if (!creator)
        return false;

    auto it = nodeCreators.find(typeHash);
    if (it == nodeCreators.end()) {
        nodeCreators.emplace(typeHash, std::move(creator));
        return true;
    }

    return false;
}

Ref<Node> SerializableFactory::createNode(std::size_t typeHash, Node::UUIDType const &uuid) {
    auto it = nodeCreators.find(typeHash);
    if (it != nodeCreators.end()) {
        auto creator = it->second;
        auto node = creator();
        node->updateUUID(uuid);
        return node;
    }

    return nullptr;
}
} // namespace krd
