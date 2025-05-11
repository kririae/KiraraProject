#pragma once

#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixin.h"
#include "range/v3/view/concat.hpp"
#include "range/v3/view/single.hpp"

namespace krd {
class SceneRoot : public NodeMixin<SceneRoot, Node> {
public:
    [[nodiscard]] static Ref<SceneRoot> create() { return {new SceneRoot}; }
    ~SceneRoot() override = default;

public:
    ranges::any_view<Ref<Node>> traverse(Visitor &visitor) override {
        (void)(visitor);
        return ranges::views::concat(
            ranges::views::single(meshGroup), ranges::views::single(geomGroup)
        );
    }

    ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const override {
        (void)(visitor);
        return ranges::views::concat(
            ranges::views::single(meshGroup), ranges::views::single(geomGroup)
        );
    }

    Ref<Group> getMeshGroup() { return meshGroup; }
    Ref<Group> getGeomGroup() { return geomGroup; }

protected:
    SceneRoot() : meshGroup(Group::create()), geomGroup(Group::create()) {}

private:
    /// \brief A list of all the meshes in the scene.
    ///
    /// Each subtree of this group represents a hierarchy of structure and should not contain other
    /// types of nodes.
    Ref<Group> meshGroup;

    /// A hierarchy of geometries
    Ref<Group> geomGroup;
};
} // namespace krd
