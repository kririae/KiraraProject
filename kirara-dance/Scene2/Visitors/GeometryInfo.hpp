#pragma once

#include "Scene2/Geometry.h"
#include "Scene2/SceneRoot.h"
#include "Scene2/Transform.h"
#include "SceneGraph/Group.h"
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

    void apply(Node const &t) override { t.traverse(*this); }

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
