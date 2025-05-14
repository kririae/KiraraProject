#pragma once

#include <mutex>
#include <range/v3/view/any_view.hpp>

#include "Core/Object.h"
#include "Visitors.h"

namespace krd {
/// \brief Base class for all nodes in the scene graph.
///
/// A Node represents an element within a hierarchical scene structure.
/// It supports operations like accepting visitors for traversal and inspection.
class Node : public Object {
public:
    ~Node() override = default;

    /// \brief Accepts a non-const visitor.
    ///
    /// This method is part of the visitor design pattern and allows a `Visitor`
    /// to operate on this node.
    /// \param visitor The non-const visitor to accept.
    virtual void accept(Visitor &visitor) { visitor.apply(*this); }

    /// \brief Accepts a const visitor.
    ///
    /// This method is part of the visitor design pattern and allows a `ConstVisitor`
    /// to operate on this node without modifying it.
    /// \param visitor The const visitor to accept.
    virtual void accept(ConstVisitor &visitor) const { visitor.apply(*this); }

    /// \brief Traverses the children of this node using a non-const visitor.
    ///
    /// This method returns a view of the direct children of this node. The visitor
    /// can use this view to continue traversal or gather information.
    /// The default implementation returns an empty view, indicating no children.
    ///
    /// \note The visitor is responsible for implementing the actual traversal logic
    ///       (e.g., depth-first, breadth-first) and should not modify the node itself
    ///       during this specific `traverse` call. Modifications should typically
    ///       happen within the visitor's `apply` methods.
    /// \param visitor The non-const visitor to guide the traversal.
    /// \return A `ranges::any_view` of `Ref<Node>` representing the children.
    [[nodiscard]] virtual ranges::any_view<Ref<Node>> traverse(Visitor &visitor) {
        (void)(visitor);
        return {};
    }

    /// \brief Traverses the children of this node using a const visitor.
    ///
    /// This method returns a view of the direct children of this node. The const visitor
    /// can use this view to continue traversal or gather information without modifying
    /// any part of the scene graph.
    /// The default implementation returns an empty view, indicating no children.
    ///
    /// \note The visitor is responsible for implementing the actual traversal logic.
    /// \param visitor The const visitor to guide the traversal.
    /// \return A `ranges::any_view` of `Ref<Node>` representing the children.
    [[nodiscard]] virtual ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const {
        (void)(visitor);
        return {};
    }

    /// \brief Clones the node.
    ///
    /// The returned node is a shallow copy of the original node.
    ///
    /// \remark The node is not cloned recursively, and might return a nullptr if cloned is not
    /// supported.
    [[nodiscard]] virtual Ref<Node> clone() const { return nullptr; }

    /// \brief Replace a node with another node.
    ///
    /// If the node is not found, this function does nothing.
    /// The function is non-recursive thus does not traverse the children.
    virtual void replace(uint64_t oldId, Ref<Node> const &newNode) {}

    /// \brief Returns the unique identifier of the node.
    ///
    /// This ID is generated from a global atomic counter, ensuring uniqueness
    /// across all node instances. IDs are non-decreasing and are not reused.
    ///
    /// \remark This ID can be used to uniquely identify nodes, for example, in graph visualizations
    /// or debugging.
    [[nodiscard]] auto getId() const { return id; }

    /// \brief Gets the specific type name of the node.
    ///
    /// This is a pure virtual function that must be implemented by derived classes
    /// to return a string representation of their type (e.g., "MeshNode", "LightNode").
    ///
    /// \see NodeMixin::getTypeName() for a potential helper mixin.
    /// \return A string representing the node's type.
    [[nodiscard]] virtual std::string getTypeName() const = 0;

    /// \brief Gets a human-readable string representation of the node.
    ///
    /// By default, this returns a string containing the node's type name and its unique ID.
    /// Derived classes can override this to provide more specific information.
    /// \return A human-readable string.
    [[nodiscard]] virtual std::string getHumanReadable() const {
        return std::format("[{} ({})]", getTypeName(), getId());
    }

    /// \brief A mutex to protect the whole scene graph.
    ///
    /// The Global Scene Lock (GSL) is used to protect the scene graph from concurrent access.
    mutable std::mutex GSL;

public:
    Node(Node const &other) : Object(other), id(nodeCount.fetch_add(1)) {
        // The GSL member (std::mutex) is not copyable.
        // It will be default-initialized in the new Node object, meaning 'this->GSL' will be a new,
        // unlocked mutex.
    }

protected:
    /// \brief Global atomic counter to generate unique node IDs.
    static inline std::atomic_uint64_t nodeCount;
    /// \brief The unique identifier for this node instance.
    uint64_t id{0};

    ///
    // uint64_t key{0};
    // uint64_t range{0};

    /// \brief Protected default constructor.
    ///
    /// Nodes are typically not meant to be created directly but rather through derived classes
    /// or factory methods. This constructor initializes the unique `id` for the node
    /// by incrementing the global `nodeCount`.
    Node() noexcept {
        // Increment the node count when a new node is created.
        id = nodeCount.fetch_add(1);
    }
};
} // namespace krd
