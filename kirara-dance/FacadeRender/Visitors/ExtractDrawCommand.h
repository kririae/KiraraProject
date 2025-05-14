#pragma once

#include "Core/Math.h"
#include "FacadeRender/TriMeshResource.h"
#include "Scene/Geometry.h"
#include "Scene/SceneRoot.h"
#include "Scene/Transform.h"
#include "SceneGraph/Visitors.h"
#include "SceneGraph/Visitors/ExtractTypeOf.h"

namespace krd {
/// \brief A visitor to issue draw commands for the geometry hierarchy.
///
/// This is the major visitor to the immediate render. This traverse the geometry hierarchy and
/// issues the corresponding draw command. This visitor is designed to be quite robust when the
/// scene graph is not in a valid state.
class ExtractDrawCommand final : public ConstVisitor {
public:
    using CallbackType = std::function<void(TriMeshResource const *, float4x4)>;
    ExtractDrawCommand(CallbackType callback) : drawCallback(std::move(callback)) {}

    /// \brief Create a new ExtractDrawCommand visitor.
    ///
    /// \remark ExtractDrawCommand can be created on stack or heap.
    [[nodiscard]] static Ref<ExtractDrawCommand> create(CallbackType callback) {
        return {new ExtractDrawCommand(std::move(callback))};
    }

public:
    void apply(SceneRoot const &val) override {
        // Traverse only the geometry group.
        auto children = val.getGeomGroup()->getChildren();
        for (auto const &child : children)
            child->accept(*this);
    }

    void apply(Group const &val) override {
        // Continue traversing the group.
        traverse(val);
    }

    void apply(Transform const &val) override {
        // In case of the transform node, we cache the model matrix first.
        auto cachedModelMatrix = modelMatrix;
        modelMatrix = mul(modelMatrix, val.getMatrix());
        traverse(val);
        modelMatrix = cachedModelMatrix;
    }

    void apply(Geometry const &val) override {
        // When the leaf node is encountered, we issue the draw command.
        issue(val);
    }

private:
    CallbackType drawCallback;
    float4x4 modelMatrix{linalg::identity};

    /// Specifically issue the draw command for the geometry.
    void issue(Geometry const &val) {
        // Find the mesh in the geometry.
        if (!val.getMesh())
            return;

        // Find the \c TriMeshResource in the mesh.
        auto mesh = val.getDynamicMesh() ? val.getDynamicMesh() : val.getMesh();
        ExtractTypeOf<TriMeshResource const> extractor;
        mesh->accept(extractor);

        if (extractor.size() != 1) {
            LogTrace("ExtractDrawCommand: The mesh has no resource or multiple resources.");
            return;
        }

        // Issue the draw command
        auto const &triMeshResource = extractor.front();
        drawCallback(triMeshResource.get(), float4x4{modelMatrix});
    }
};
} // namespace krd
