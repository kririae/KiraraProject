#pragma once

#include "Core/Math.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"
#include "SceneGraph/Visitors.h"

namespace krd {
struct SkelVertex {
    float position[3];
};

class EXTDebugSkeleton : public ConstVisitor, public kira::SmallVector<std::pair<float3, float3>> {
public:
    struct Desc {
        /// \brief The starting depth of the skeleton.
        ///
        /// For example, when the starting depth is 0, the skeleton will be drawn from the root
        /// node. When the starting depth is 1, the skeleton will be drawn from the first child of
        /// the root
        uint32_t startingDepth = 0;
    };

    explicit EXTDebugSkeleton(Desc const &desc) : desc(desc) {}

    [[nodiscard]] static Ref<EXTDebugSkeleton> create(Desc const &desc) {
        return {new EXTDebugSkeleton(desc)};
    }

public:
    void apply(SceneRoot const &val) override {
        auto children = val.getGeomGroup()->getChildren();

        // For all "geometry instances"
        for (auto const &child : children)
            child->accept(*this);
    }

    void apply(Group const &val) override { traverse(val); }
    void apply(Transform const &val) override {
        constexpr auto homo2world = [](float4 const &v) { return float3{v.x, v.y, v.z} / v.w; };

        auto cachedModelMatrix = this->modelMatrix;
        auto cachedParentTranslation = this->parentTranslation;
        modelMatrix = mul(modelMatrix, val.getMatrix());

        auto currTranslation = homo2world(mul(modelMatrix, float4{0.0f, 0.0f, 0.0f, 1.0f}));

        // Only draw the skeleton when the depth is greater than or equal to the starting depth.
        if (depth >= desc.startingDepth)
            this->emplace_back(parentTranslation, currTranslation);
        this->parentTranslation = currTranslation;

        ++depth;
        traverse(val);
        --depth;

        this->modelMatrix = cachedModelMatrix;
        this->parentTranslation = cachedParentTranslation;
    }

private:
    Desc desc;

    uint32_t depth{0};
    float4x4 modelMatrix{linalg::identity};
    float3 parentTranslation{};
};
} // namespace krd
