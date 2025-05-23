#pragma once

#include <range/v3/view/any_view.hpp>

#include "Node.h"
#include "NodeMixin.h"

namespace krd {
/// \brief A group of nodes in the scene graph.
///
/// This class manages a collection of child nodes and provides functionality
/// for traversing these children. It serves as a composite node in a
/// scene graph structure.
///
/// Adding child nodes to this group is thread-safe.
class Group : public NodeMixin<Group, Node> {
public:
    /// \brief Creates a new Group instance.
    /// \return A reference-counted pointer (Ref) to the newly created Group.
    [[nodiscard]] static Ref<Group> create() { return {new Group}; }

public:
    /// \brief Adds a child node to this group.
    ///
    /// The child node is moved into the group's collection of children.
    /// \param child A reference-counted pointer (Ref) to the Node to be added.
    void addChild(Ref<Node> child) {
        std::lock_guard lock(GSL);
        children.push_back(std::move(child));
    }

#if 0
    /// \brief Gets a reference to the vector of child nodes.
    /// \return A reference to the internal storage of child nodes.
    [[nodiscard]] ranges::any_view<Ref<Node>> getChildren() { 
        std::lock_guard lock(GSL);
        return ranges::views::all(children); 
    }

    /// \brief Gets a constant reference to the vector of child nodes.
    /// \return A constant reference to the internal storage of child nodes.
    [[nodiscard]] ranges::any_view<Ref<Node>> getChildren() const {
        std::lock_guard lock(GSL);
        return ranges::views::all(children);
    }
#endif

    /// \brief Traverses the child nodes of this group.
    ///
    /// \param visitor A reference to a Visitor object.
    /// \return A ranges::any_view over the child nodes.
    ranges::any_view<Ref<Node>> traverse(Visitor &visitor) override {
        std::lock_guard lock(GSL);
        return ranges::views::all(children);
    }

    /// \brief Traverses the child nodes of this group (const version).
    ///
    /// \param visitor A reference to a ConstVisitor object.
    /// \return A ranges::any_view over the child nodes.
    ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const override {
        std::lock_guard lock(GSL);
        return ranges::views::all(children);
    }

private:
    kira::SmallVector<Ref<Node>> children;
};
} // namespace krd
