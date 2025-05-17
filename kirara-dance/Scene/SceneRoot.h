#pragma once

#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>

#include "Scene/Geometry.h"
#include "Scene/Transform.h"
#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixins.h"
#include "SceneGraph/Visitors.h"

namespace krd {
class SceneRoot : public SerializableMixin<SceneRoot, Node, "krd::SceneRoot"> {
public:
    [[nodiscard]] static Ref<SceneRoot> create() { return {new SceneRoot}; }

    ranges::any_view<Ref<Node>> traverse(Visitor &visitor) override {
        return ranges::views::concat(
            ranges::views::single(meshGroup), //
            ranges::views::single(geomGroup), //
            ranges::views::single(auxGroup)
        );
    }
    ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const override {
        return ranges::views::concat(
            ranges::views::single(meshGroup), //
            ranges::views::single(geomGroup), //
            ranges::views::single(auxGroup)
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

public:
    void archive(auto &ar) {
        ar(meshGroup);
        ar(geomGroup);
        ar(auxGroup);
    }
};

#if 0
///
class SceneInserter : public Visitor {
public:
    struct Desc {
        /// \brief The node to be inserted into the scene.
        Ref<SceneRoot> sceneRoot;
    };

    explicit SceneInserter(Desc const &desc) : sceneRoot(desc.sceneRoot) {}
    [[nodiscard]] static Ref<SceneInserter> create(Desc const &desc) {
        return {new SceneInserter(desc)};
    }

public:
    void apply(SceneRoot &val) override {
        throw kira::Anyhow("SceneInserter: SceneRoot should not be inserted into itself");
    }

    void apply(TriangleMesh &val) override { sceneRoot->getMeshGroup()->addChild(&val); }
    void apply(Transform &val) override { sceneRoot->getGeomGroup()->addChild(&val); }
    void apply(Geometry &val) override { sceneRoot->getGeomGroup()->addChild(&val); }
    void apply(Node &val) override { sceneRoot->getAuxGroup()->addChild(&val); }

private:
    Ref<SceneRoot> sceneRoot;
};
#endif
} // namespace krd
