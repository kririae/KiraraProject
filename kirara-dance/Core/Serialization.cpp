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
} // namespace krd
