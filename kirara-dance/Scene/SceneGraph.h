#pragma once

#include "Core/Object.h"
#include "Scene/Transform.h"

namespace krd {
class SceneGraph;

///
class SceneNode final : public Object {
public:
    ///
    SceneNode(std::string name, Transform const &transform, SceneNode *parent)
        : name(std::move(name)), transform(transform), parent(parent) {}

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
    void setTransform(Transform const &transform) { this->transform = transform; }
    ///
    [[nodiscard]] Transform const &getTransform() const { return transform; }

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

    Ref<SceneNode> root;
};
} // namespace krd
