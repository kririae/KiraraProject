#pragma once

#include "../Visitors.h"

namespace krd {
/// \brief A visitor that duplicates a scene graph node all the way to the root node.
///
/// This is one of the most important mechanism to implement the thread-safe model of the scene
/// graph.
class CopyOnWrite final : public Visitor {
public:
    struct Desc {
        /// The root node of the scene graph to duplicate.
        uint64_t rootId{0};

        /// The id of the nodes to duplicate.
        uint64_t id{0};
    };

    CopyOnWrite(Desc desc) : desc(desc) {}
    [[nodiscard]] static Ref<CopyOnWrite> create(Desc const &desc) {
        return {new CopyOnWrite(desc)};
    }

public:
    void apply(Node &val) override {}

private:
    Desc desc;
    Ref<Node> rootNode;
};
} // namespace krd
