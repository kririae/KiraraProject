#pragma once

#include <unordered_map>
#include <unordered_set>

#include "Core/Math.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"
#include "SceneGraph/Visitors.h"

namespace krd {
/// \brief A visitor to extract the node transforms.
///
/// This class traverses a scene graph and computes the accumulated transformation matrices for
/// specified nodes relative to specified root nodes. The results are stored in an unordered_map
/// mapping node IDs to their 4x4 transformation matrices.
///
/// \remark The transformation of the root node itself is not included in the accumulated transform.
class ExtractRelativeTransforms : public ConstVisitor,
                                  public std::unordered_map<uint64_t, float4x4> {
public:
    /// \brief Descriptor for configuring the ExtractRelativeTransforms visitor.
    struct Desc {
        /// \brief A list of root node IDs. Each transform chain will start from one of these roots.
        /// The transform of the root node itself is not included in the accumulated transform.
        kira::SmallVector<uint64_t> rootNodeIds;

        /// \brief A list of target node IDs for which to extract transforms.
        /// This list must be the same size as `rootNodeIds`, with each `nodeIds[i]`
        /// corresponding to `rootNodeIds[i]`.
        kira::SmallVector<uint64_t> nodeIds;
    };

    /// \brief Creates a new ExtractRelativeTransforms visitor.
    [[nodiscard]] static Ref<ExtractRelativeTransforms> create(Desc const &desc) {
        return {new ExtractRelativeTransforms(desc)};
    }

    /// \brief Constructs an ExtractRelativeTransforms visitor.
    ///
    /// \param desc The descriptor containing configuration for the visitor.
    ///
    /// \throws kira::Anyhow if `desc.nodeIds` and `desc.rootNodeIds` have different sizes, or if a
    /// node ID is the same as its corresponding root node ID.
    explicit ExtractRelativeTransforms(Desc const &desc) {
        // insert desc.nodeIds into the set
        if (desc.nodeIds.size() != desc.rootNodeIds.size())
            throw kira::Anyhow(
                "ExtractRelativeTransforms: The node IDs and root node IDs are not the same size"
            );
        for (size_t i = 0; i < desc.nodeIds.size(); ++i) {
            auto nodeId = desc.nodeIds[i];
            auto rootNodeId = desc.rootNodeIds[i];
            if (nodeId == rootNodeId)
                throw kira::Anyhow(
                    "ExtractRelativeTransforms: The node ID and root node ID are the same"
                );
            this->nodeIdMap.emplace(nodeId, rootNodeId);
            this->rootNodeIdSet.insert(rootNodeId);
        }
    }

public:
    /// \brief Applies the visitor to a SceneRoot.
    /// This initiates the traversal of the scene graph.
    /// \param val The SceneRoot to visit.
    void apply(SceneRoot const &val) override {
        auto children = val.getGeomGroup()->traverse(*this);
        for (auto const &child : children)
            child->accept(*this);
    }

    /// \brief Applies the visitor to a Group node.
    /// This continues the traversal to the children of the group.
    /// \param val The Group node to visit.
    void apply(Group const &val) override { traverse(val); }

    /// \brief Applies the visitor to a Transform node.
    /// This method accumulates transformations and stores the result if the current
    /// node is one of the target nodes.
    /// \param val The Transform node to visit.
    void apply(Transform const &val) override {
        auto const nodeId = val.getId();
        auto cachedTransformMap = transformMap;

        // Don't include the transform of the root node itself
        if (rootNodeIdSet.contains(nodeId) && !transformMap.contains(nodeId))
            transformMap.emplace(nodeId, linalg::identity);
        else
            for (auto &[nodeId, transform] : transformMap)
                transform = mul(transform, val.getMatrix());

        if (auto it = nodeIdMap.find(nodeId); it != nodeIdMap.end()) {
            auto rootNodeId = it->second;

            // Find the accumulated transform towards here.
            auto transform = transformMap.at(rootNodeId);
            this->emplace(nodeId, transform);
        }

        traverse(val);

        // We're going back up the hierarchy, so restore the transform map
        // to maintain correct transform chains
        transformMap = std::move(cachedTransformMap);
    }

private:
    /// \brief The map between a root node ID and its current accumulated transform.
    /// This is used during traversal to build up the transform chain.
    std::unordered_map<uint64_t, float4x4> transformMap;

    /// \brief The map between a target node ID and its corresponding root node ID.
    std::unordered_map<uint64_t, uint64_t> nodeIdMap;

    /// \brief A set of all root node IDs for quick lookup.
    std::unordered_set<uint64_t> rootNodeIdSet;
};
} // namespace krd
