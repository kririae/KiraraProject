#pragma once

#include <list>

#include "Core/Object.h"
#include "Scene/Transform.h"

namespace krd {
class SceneGraph;

///
class SceneNode final : public Object {
    ///
    SceneNode(std::string name, Transform const &transform, SceneNode *parent)
        : name(std::move(name)), transform(transform), parent(parent) {}

public:
    static Ref<SceneNode> create(std::string name, Transform const &transform, SceneNode *parent) {
        return {new SceneNode(std::move(name), transform, parent)};
    }

    ///
    ~SceneNode() override {
        // Cleanup owning references first.
        children.clear();
    }

    ///
    void setParent(SceneNode *parent) { this->parent = parent; }
    ///
    [[nodiscard]] SceneNode *getParent() const { return parent; }
    ///
    void setLocalTransform(Transform const &transform) { this->transform = transform; }
    ///
    [[nodiscard]] Transform const &getLocalTransform() const { return transform; }

    /// \brief Returns the global transformation matrix.
    ///
    /// \note Sometimes it is not a good idea to use this function in a loop because it is not
    /// efficient, consider using \c getGlobalPosition instead.
    /// \note \c transform turns the coordinate into the parent's space.
    [[nodiscard]] float4x4 getGlobalTransformMatrix() const;

    /// Transform a position from the local space to the global space.
    [[nodiscard]] float3 getGlobalPosition(float3 position) const;

    ///
    void addChild(Ref<SceneNode> const &child) {
        children.push_back(child);
        child->setParent(this);
    }

private:
    ///
    std::string name;
    ///
    Transform transform;

    ///
    SceneNode *parent{nullptr};
    /// A list of references to children of this node.
    std::list<Ref<SceneNode>> children;
};

class SceneGraph final {
public:
    ///
    SceneGraph() = default;

    ///
    ~SceneGraph() = default;

    ///
    Ref<SceneNode> root;
};
} // namespace krd
