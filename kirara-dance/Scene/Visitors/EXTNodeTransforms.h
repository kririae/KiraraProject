#pragma once

#include <unordered_map>
#include <unordered_set>

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
        /// All the chains of transforms will be accumulated all at once.
        kira::SmallVector<uint64_t> rootNodeIds;
        kira::SmallVector<uint64_t> nodeIds;
    };

    ///
    [[nodiscard]] static Ref<EXTNodeTransforms> create(Desc const &desc) {
        return {new EXTNodeTransforms(desc)};
    }

    explicit EXTNodeTransforms(Desc const &desc) {
        // insert desc.nodeIds into the set
        if (desc.nodeIds.size() != desc.rootNodeIds.size())
            throw kira::Anyhow(
                "EXTNodeTransforms: The node IDs and root node IDs are not the same size"
            );
        for (size_t i = 0; i < desc.nodeIds.size(); ++i) {
            auto nodeId = desc.nodeIds[i];
            auto rootNodeId = desc.rootNodeIds[i];
            if (nodeId == rootNodeId)
                throw kira::Anyhow("EXTNodeTransforms: The node ID and root node ID are the same");
            this->nodeIdMap.emplace(nodeId, rootNodeId);
            this->rootNodeIdSet.insert(rootNodeId);
        }
    }

public:
    void apply(SceneRoot const &val) override {
        auto children = val.getGeomGroup()->getChildren();
        for (auto const &child : children)
            child->accept(*this);
    }

    void apply(Group const &val) override { traverse(val); }
    void apply(Transform const &val) override {
        auto cachedTransformMap = transformMap;

        if (rootNodeIdSet.contains(val.getId()) && !transformMap.contains(val.getId()))
            transformMap.emplace(val.getId(), linalg::identity);

        // Accumulate the transform.
        for (auto &[nodeId, transform] : transformMap)
            transform = mul(transform, val.getMatrix());

        if (auto it = nodeIdMap.find(val.getId()); it != nodeIdMap.end()) {
            auto rootNodeId = it->second;

            // Find the accumulated transform towards here.
            auto transform = transformMap.at(rootNodeId);
            this->emplace(val.getId(), transform);
        }

        traverse(val);
        transformMap = std::move(cachedTransformMap);
    }

private:
    /// The map between the node ID and the accumulated transform
    std::unordered_map<uint64_t, float4x4> transformMap;
    /// The map between the node ID and the rootNodeId
    std::unordered_map<uint64_t, uint64_t> nodeIdMap;
    std::unordered_set<uint64_t> rootNodeIdSet;
};
} // namespace krd
