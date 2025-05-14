#pragma once

#include <map>
#include <string>

#include "SceneGraph/Node.h"
#include "SceneGraph/Visitors.h"

namespace krd {
/// \brief Tree Checker is used to check if the traversal graph is indeed a tree from this node.
///
/// The \a ValidateTreeHierarchy is a stateful visitor that keeps track of visited nodes.
class ValidateTreeHierarchy final : public ConstVisitor {
public:
    /// Create a new ValidateTreeHierarchy visitor.
    ///
    /// \remark ValidateTreeHierarchy can be created on stack or heap.
    ValidateTreeHierarchy() = default;
    [[nodiscard]] static Ref<ValidateTreeHierarchy> create() { return {new ValidateTreeHierarchy}; }

    ~ValidateTreeHierarchy() override = default;

public:
    /// Check if the graph is a valid tree.
    bool isValidTree() const { return this->isTree; }

    /// Get a diagnostic message about the tree.
    ///
    /// Only when \c isValidTree returns false, this function will give a valid message.
    std::string_view getDiagnostic() const { return diagnostic; }

    /// Reset the visitor state to check a new tree.
    void clear() {
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
