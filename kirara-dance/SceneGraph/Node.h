#pragma once

#include "Core/Object.h"
#include "Visitors.h"
#include "range/v3/view/any_view.hpp"

namespace krd {
/// \brief A node in the scene graph.
///
class Node : public Object {
public:
    ~Node() override = default;

    ///
    virtual void accept(Visitor &visitor) { visitor.apply(*this); }

    /// \copydoc accept(Visitor &visitor)
    virtual void accept(ConstVisitor &visitor) const { visitor.apply(*this); }

    /// \brief Traverse the node and apply the visitor to all children.
    ///
    /// This returns a view of the children of the node, that the visitor can used to record the
    /// tree information or go deeper.
    ///
    /// \note The visitor itself should take care of the traversal and not to modify the node
    /// itself.
    [[nodiscard]] virtual ranges::any_view<Ref<Node>> traverse(Visitor &visitor) {
        (void)(visitor);
        return {};
    }

    /// \copydoc traverse(Visitor &visitor)
    [[nodiscard]] virtual ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const {
        (void)(visitor);
        return {};
    }

    /// \brief Return the ID of the node accumulated from the node count.
    ///
    /// \remark This can be used to identify the node in the graphs.
    /// \remark IDs are not reused and is non-decreasing.
    [[nodiscard]] auto getId() const { return id; }

    /// \brief Get the type name of the node.
    ///
    /// \see NodeMixin::getTypeName()
    [[nodiscard]] virtual std::string getTypeName() const = 0;

protected:
    static inline std::atomic_uint64_t nodeCount;
    uint64_t id{0}; // The ID of the node. This is used to identify the node in the scene graph.

    /// Node should not be directly created.
    Node() noexcept {
        // Increment the node count when a new node is created.
        id = nodeCount.fetch_add(1);
    }
};
} // namespace krd
