#pragma once

#include "Core/Math.h"
#include "Scene/Geometry.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"
#include "SceneGraph/Visitors.h"

namespace krd {
class ISTSkinnedMesh : public ConstVisitor {
public:
    struct Desc {};

public:
    void apply(SceneRoot const &val) override {
        auto children = val.getGeomGroup()->getChildren();
        for (auto const &child : children)
            child->accept(*this);
    }

    void apply(Group const &val) override { traverse(val); }
    void apply(Transform const &val) override {
        auto cachedModelMatrix = this->modelMatrix;
        modelMatrix = mul(modelMatrix, val.getMatrix());
        traverse(val);
        modelMatrix = cachedModelMatrix;
    }

    void apply(Geometry const &val) override {
        auto cachedModelMatrix = this->modelMatrix;
        modelMatrix = mul(modelMatrix, val.getMatrix());
        traverse(val);
        modelMatrix = cachedModelMatrix;
    }

private:
    Desc desc;
    float4x4 modelMatrix{linalg::identity};
};
} // namespace krd
