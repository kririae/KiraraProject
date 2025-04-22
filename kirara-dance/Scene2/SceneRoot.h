#pragma once

#include "SceneGraph/Group.h"
#include "SceneGraph/Node.h"

namespace krd {
class SceneRoot : public Node {
public:
    [[nodiscard]] static Ref<SceneRoot> create() { return {new SceneRoot}; }
    ~SceneRoot() override = default;

public:
    void accept(Visitor &visitor) override { visitor.apply(*this); }
    void traverse(Visitor &visitor) override {
        meshGroup->accept(visitor);
        geomGroup->accept(visitor);
    }
    void accept(ConstVisitor &visitor) const override { visitor.apply(*this); }
    void traverse(ConstVisitor &visitor) const override {
        meshGroup->accept(visitor);
        geomGroup->accept(visitor);
    }

    Ref<Group> getMeshGroup() { return meshGroup; }
    Ref<Group> getGeomGroup() { return geomGroup; }

protected:
    SceneRoot() : meshGroup(Group::create()), geomGroup(Group::create()) {}

private:
    /// A list of all the meshes in the scene
    Ref<Group> meshGroup;
    /// A hierarchy of geometries
    Ref<Group> geomGroup;
};
} // namespace krd
