#pragma once

#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>

#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixin.h"

namespace krd {
class SceneRoot : public NodeMixin<SceneRoot, Node> {
public:
    [[nodiscard]] static Ref<SceneRoot> create() { return {new SceneRoot}; }

public:
    ranges::any_view<Ref<Node>> traverse(Visitor &visitor) override {
        return ranges::views::concat(
            meshGroup->traverse(visitor), geomGroup->traverse(visitor), auxGroup->traverse(visitor)
        );
    }
    ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const override {
        return ranges::views::concat(
            meshGroup->traverse(visitor), geomGroup->traverse(visitor), auxGroup->traverse(visitor)
        );
    }

    Ref<Group> getMeshGroup() { return meshGroup; }
    Ref<Group> getGeomGroup() { return geomGroup; }
    Ref<Group> getAuxGroup() { return auxGroup; }
    Ref<Group> getMeshGroup() const { return meshGroup; }
    Ref<Group> getGeomGroup() const { return geomGroup; }
    Ref<Group> getAuxGroup() const { return auxGroup; }

protected:
    SceneRoot() = default;

private:
    /// \brief A list of all the meshes in the scene.
    ///
    /// Each subtree of this group represents a hierarchy of structure and should not contain other
    /// types of nodes.
    Ref<Group> meshGroup{Group::create()};

    /// A hierarchy of geometries
    Ref<Group> geomGroup{Group::create()};

    /// A flat list of auxiliary groups.
    Ref<Group> auxGroup{Group::create()};
};
} // namespace krd
