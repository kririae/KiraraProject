#pragma once

#include "Core/Object.h"

namespace krd {
class Node;
class Group;

class Animation;
class Geometry;
class SceneRoot;
class Transform;
class TriangleMesh;
class TransformAnimationChannel;

class TriangleMeshResource;

template <typename T>
concept IsNode = std::derived_from<T, Node> || std::is_same_v<T, Node>;

/// \brief A visitor for the scene graph.
///
/// When you inherit the Visitor class, you can override the \c apply methods or add new virtual
/// functions.
///
/// By default, the visitor will not recurse into the children of the node. Recursion is done by
/// invoking the \c traverse method of the node.
///
/// When you override the \c apply method of Node by calling the \c traverse method, you can get the
/// capability of full recursion.
class Visitor : public Object {
public:
    virtual ~Visitor() = default;

    // General nodes
    virtual void apply(Node &val);
    virtual void apply(Group &val);

    // Scene nodes
    virtual void apply(Animation &val);
    virtual void apply(SceneRoot &val);
    virtual void apply(Transform &val);
    virtual void apply(Geometry &val);
    virtual void apply(TriangleMesh &val);
    virtual void apply(TransformAnimationChannel &val);

    // Immediate render nodes
    virtual void apply(TriangleMeshResource &val);

protected:
    Visitor() = default;

    /// A shortcut to continue the traversal of the node.
    template <IsNode T> void traverse(T &val) {
        auto children = val.traverse(*this);
        for (auto const &child : children)
            child->accept(*this);
    }
};

/// \brief A const visitor for the scene graph.
///
/// \copydoc Visitor
class ConstVisitor : public Object {
public:
    virtual ~ConstVisitor() = default;

    // General nodes
    virtual void apply(Node const &val);
    virtual void apply(Group const &val);

    // Scene nodes
    virtual void apply(Animation const &val);
    virtual void apply(Geometry const &val);
    virtual void apply(SceneRoot const &val);
    virtual void apply(Transform const &val);
    virtual void apply(TriangleMesh const &val);
    virtual void apply(TransformAnimationChannel const &val);

    virtual void apply(TriangleMeshResource const &val);

protected:
    ConstVisitor() = default;

    /// A shortcut to continue the traversal of the node.
    template <IsNode T> void traverse(T const &val) {
        auto children = val.traverse(*this);
        for (auto const &child : children)
            child->accept(*this);
    }
};
} // namespace krd
