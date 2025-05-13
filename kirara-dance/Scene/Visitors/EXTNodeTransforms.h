#pragma once

#include "Core/Math.h"
#include "Scene/Geometry.h"
#include "Scene/SceneRoot.h"
#include "SceneGraph/Visitors.h"

namespace krd {
/// \brief A visitor to extract the node transforms.
class EXTNodeTransforms : public ConstVisitor, public std::unordered_map<uint64_t, float4x4> {
public:
    ///
    struct Desc {
        float4x4 offset;
    };

    ///
    [[nodiscard]] static Ref<EXTNodeTransforms> create(Desc const &desc) {
        return {new EXTNodeTransforms(desc)};
    }

    explicit EXTNodeTransforms(Desc const &desc) : modelMatrix(desc.offset) {}

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
        this->emplace(val.getId(), modelMatrix);
        traverse(val);
        modelMatrix = cachedModelMatrix;
    }

private:
    float4x4 modelMatrix{linalg::identity};
};
} // namespace krd
