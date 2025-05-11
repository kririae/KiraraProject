#pragma once

#include <map>
#include <string>

#include "SceneGraph/Node.h"
#include "SceneGraph/Visitors.h"

namespace krd {
/// \brief Tree Checker is used to check if the traversal graph is indeed a tree from this node.
///
/// The \a TreeChecker is a stateful visitor that keeps track of visited nodes.
class TreeChecker final : public ConstVisitor {
public:
    /// Create a new TreeChecker visitor.
    ///
    /// \remark TreeChecker can be created on stack or heap.
    TreeChecker() = default;
    [[nodiscard]] static Ref<TreeChecker> create() { return {new TreeChecker}; }

    ~TreeChecker() override = default;

public:
    /// Check if the graph is a valid tree.
    bool isValidTree() const { return this->isTree; }

    /// Get a diagnostic message about the tree.
    ///
    /// Only when \c isValidTree returns false, this function will give a valid message.
    std::string_view getDiagnostic() const { return diagnostic; }

    /// Reset the visitor state to check a new tree.
    void reset() {
        isTree = true;
        diagnostic.clear();
        parents.clear();
    }

public:
    /// \brief Check if the node is already visited.
    ///
    /// This function guarantees to work correctly even in the presence of cycles.
    void apply(Node const &t) override {
        auto children = t.traverse(*this);
        for (auto const &child : children) {
            auto p = parents.find(child->getId());
            if (p == parents.end()) {
                parents.insert({child->getId(), &t});
                child->accept(*this);
                continue;
            }

            this->isTree = false;
            diagnostic = std::format(
                "Node {} ({}) already referenced by {} ({}), but is referenced again by {} "
                "({})",
                child->getTypeName(), child->getId(), p->second->getTypeName(), p->second->getId(),
                t.getTypeName(), t.getId()
            );
            return;
        }
    }

private:
    bool isTree{true};
    std::string diagnostic;
    std::map<uint64_t, Node const *> parents;
};
} // namespace krd
