#pragma once

#include "Core/Math.h"
#include "Scene/Geometry.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"
#include "SceneGraph/Visitors.h"

namespace krd {
class ISTSkinnedMesh : public Visitor {
public:
    struct Desc {};

public:
    void apply(SceneRoot &val) override {
        this->root = &val;

        auto children = val.getGeomGroup()->getChildren();
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
        auto cachedModelMatrix = this->modelMatrix;
        modelMatrix = mul(modelMatrix, val.getMatrix());

        if (auto mesh = val.getMesh(); root && mesh && mesh->hasWeights()) {
            // Reset the older mesh
            val.getDynamicMesh().reset();
            val.linkDynamicMesh(mesh->adaptLinearBlendSkinning(root, float4x4{identity}));

            // TODO(krr): temporary solution for the new mesh to be reachable
            root->getMeshGroup()->addChild(val.getDynamicMesh());
        }

        traverse(val);
        modelMatrix = cachedModelMatrix;
    }

private:
    Desc desc;
    Ref<SceneRoot> root;
    float4x4 modelMatrix{linalg::identity};
};
} // namespace krd
