#pragma once

#include "../TriMeshResource.h"
#include "Scene/TriangleMesh.h"
#include "SceneGraph/Visitors.h"
#include "SceneGraph/Visitors/ExtractTypeOf.h"

namespace krd {
/// \brief A visitor to populate the resource of a triangle mesh.
///
/// This transforms a general tree into a renderable tree, e.g., insert the \c TriMeshResource
/// under the \c TriangleMesh.
class InsertTriMeshResource : public Visitor {
public:
    InsertTriMeshResource(Ref<SlangGraphicsContext> SGC) : SGC(std::move(SGC)) {}

    [[nodiscard]] static Ref<InsertTriMeshResource> create(Ref<SlangGraphicsContext> SGC) {
        return {new InsertTriMeshResource(std::move(SGC))};
    }

public:
    void apply(Node &val) override { traverse(val); }
    void apply(TriangleMesh &val) override {
        // Only attach the resource if it is not already attached.
        // Don't worry about dirty state, the resource will be cleared and re-uploaded.
        ExtractTypeOf<TriMeshResource const> ext;
        val.accept(ext);
        if (ext.empty()) {
            auto res = TriMeshResource::create();
            res->uploadTriMesh(&val, SGC.get());
            val.addChild(res);
        }
    }

private:
    Ref<SlangGraphicsContext> SGC;
};
} // namespace krd
