#pragma once

#include "Core/Object.h"
#include "Visitors.h"

namespace krd {
/// \brief A node in the scene graph.
///
class Node : public Object {
public:
    ~Node() override = default;

    ///
    virtual void accept(Visitor &visitor) { visitor.apply(*this); }
    virtual void traverse(Visitor &visitor) {}

    ///
    virtual void accept(ConstVisitor &visitor) const { visitor.apply(*this); }
    virtual void traverse(ConstVisitor &visitor) const {}

    /// \brief Return the ID of the node accumulated from the node count.
    ///
    /// \remark This can be used to identify the node in the graphs.
    /// \remark IDs are not reused and is non-decreasing.
    [[nodiscard]] auto getId() const { return id; }

protected:
    static inline std::atomic_uint64_t nodeCount;
    uint64_t id{0}; // The ID of the node. This is used to identify the node in the scene graph.

    /// Node should not be directly created.
    Node() noexcept {
        // Increment the node count when a new node is created.
        nodeCount.fetch_add(1);
    }
};
} // namespace krd
