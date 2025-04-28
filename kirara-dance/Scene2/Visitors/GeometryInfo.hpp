#pragma once

#include "Scene2/Geometry.h"
#include "SceneGraph/Node.h"

namespace krd {
class GeometryInfo final : public ConstVisitor {
public:
    /// Create a new PrintStatsVisitor.
    ///
    /// \remark PrintStatsVisitor can be created on stack or heap.
    GeometryInfo() = default;

    [[nodiscard]] static Ref<GeometryInfo> create() { return {new GeometryInfo}; }

    ~GeometryInfo() override = default;

    void apply(Node const &t) override {
        LogInfo("{}", t.getId());
        auto child = t.traverse(*this);
        auto const currId = t.getId();
        for (auto const &node : child)
            if (node->getId() != currId)
                node->accept(*this);
            else
                LogInfo("Recursive NodeID encountered: {:d}", node->getId());
    }

    void apply(Geometry const &t) override {
        LogInfo("Geometry is reached");
        if (t.getMesh()) {
            LogInfo("Mesh is linked");
            LogInfo("Num vertices: {:d}", t.getMesh()->getNumVertices());
            LogInfo("Num faces: {:d}", t.getMesh()->getNumFaces());
        } else {
            LogInfo("No mesh is linked");
        }
    }
};
} // namespace krd
