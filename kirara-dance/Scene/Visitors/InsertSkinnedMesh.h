#pragma once

#include "Core/Math.h"
#include "Scene/Geometry.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"
#include "SceneGraph/Visitors.h"

namespace krd {
class InsertSkinnedMesh : public Visitor {
public:
    struct Desc {};

    explicit InsertSkinnedMesh(Desc const &desc) : desc(desc) {}
    [[nodiscard]] static Ref<InsertSkinnedMesh> create(Desc const &desc) {
        return {new InsertSkinnedMesh(desc)};
    }

public:
    void apply(SceneRoot &val) override {
        root = &val;
        auto children = val.getGeomGroup()->traverse(*this);
        for (auto const &child : children)
            child->accept(*this);
    }

    void apply(Group &val) override { traverse(val); }
    void apply(Transform &val) override {
        auto cachedModelMatrix = this->modelMatrix;
        modelMatrix = mul(modelMatrix, val.getMatrix());
        traverse(val);
        modelMatrix = cachedModelMatrix;
    }

    void apply(Geometry &val) override {
        if (auto mesh = val.getMesh(); root && mesh && mesh->hasWeights()) {
            val.getDynamicMesh().reset();

            auto newMesh = mesh->adaptLinearBlendSkinning(root);
            val.linkDynamicMesh(newMesh);

            // Attach the mesh to the transient resource graph.
            root->getMeshGroup()->addChild(val.getDynamicMesh());
        }
    }

private:
    Desc desc;
    Ref<SceneRoot> root;
    float4x4 modelMatrix{linalg::identity};
};
} // namespace krd
