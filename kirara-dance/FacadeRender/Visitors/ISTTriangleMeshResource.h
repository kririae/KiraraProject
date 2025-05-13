#pragma once

#include "../TriangleMeshResource.h"
#include "Scene/TriangleMesh.h"
#include "SceneGraph/Visitors.h"
#include "SceneGraph/Visitors/ExtractTypeOf.h"

namespace krd {
/// \brief A visitor to populate the resource of a triangle mesh.
///
/// This transforms a general tree into a renderable tree, e.g., insert the \c TriangleMeshResource
/// under the \c TriangleMesh.
class ISTTriangleMeshResource final : public Visitor {
public:
    ISTTriangleMeshResource(SlangGraphicsContext *context) : SGC(context) {}

    [[nodiscard]] static Ref<ISTTriangleMeshResource> create(SlangGraphicsContext *context) {
        return {new ISTTriangleMeshResource(context)};
    }

public:
    void apply(Node &val) override { traverse(val); }
    void apply(TriangleMesh &val) override {
        // Only attach the resource if it is not already attached.
        // Don't worry about dirty state, the resource will be cleared and re-uploaded.
        ExtractTypeOf<TriangleMeshResource const> ext;
        val.accept(ext);
        if (ext.empty()) {
            auto res = TriangleMeshResource::create();
            res->uploadTriMesh(&val, SGC);
            val.addChild(res);
        }
    }

private:
    SlangGraphicsContext *SGC{nullptr};
};
} // namespace krd
