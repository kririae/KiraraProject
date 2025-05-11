#pragma once

#include "Scene2/TriangleMesh.h"
#include "SceneGraph/Visitors.h"
#include "TriangleMeshResource.h"

namespace krd {
/// \brief A visitor to populate the resource of a triangle mesh.
///
/// This transforms a general tree into a renderable tree, e.g., insert the \c TriangleMeshResource
/// under the \c TriangleMesh.
class PopulateResource final : public Visitor {
public:
    PopulateResource(SlangGraphicsContext *context) : context(context) {}
    [[nodiscard]] static Ref<PopulateResource> create(SlangGraphicsContext *context) {
        return {new PopulateResource(context)};
    }
    ~PopulateResource() override = default;

public:
    void apply(Node &t) override {
        auto children = t.traverse(*this);
        for (auto const &child : children)
            child->accept(*this);
    }

    void apply(TriangleMesh &triMesh) override {
        auto res = TriangleMeshResource::create();
        res->upload(&triMesh, context);
        triMesh.addChild(res);
    }

private:
    SlangGraphicsContext *context{nullptr};
};
} // namespace krd
