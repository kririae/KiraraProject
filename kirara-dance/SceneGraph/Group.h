#pragma once

#include "Node.h"

namespace krd {
class Group : public Node {
public:
    /// \brief Create a new group.
    /// \return A reference to the new group.
    [[nodiscard]] static Ref<Group> create() { return {new Group}; }

    ~Group() override = default;

    /// \brief Add a child node to the group.
    ///
    /// \param child The child node to add.
    void addChild(Ref<Node> child) { children.push_back(std::move(child)); }

    ///
    std::vector<Ref<Node>> &getChildren() { return children; }

    ///
    [[nodiscard]] std::vector<Ref<Node>> const &getChildren() const { return children; }

    /// Accept a visitor and visit all children.
    void accept(Visitor &visitor) override { visitor.apply(*this); }

    /// Accept a const visitor and visit all children.
    void accept(ConstVisitor &visitor) const override { visitor.apply(*this); }

    /// Traverse the group and apply the visitor to all children.
    void traverse(Visitor &visitor) override {
        for (auto &child : children)
            child->accept(visitor);
    }

    /// Traverse the group and apply the visitor to all children.
    void traverse(ConstVisitor &visitor) const override {
        for (auto const &child : children)
            child->accept(visitor);
    }

private:
    std::vector<Ref<Node>> children;
};
} // namespace krd
