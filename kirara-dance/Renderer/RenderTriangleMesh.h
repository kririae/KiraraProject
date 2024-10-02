#pragma once

#include "Renderer/RenderObject.h"
#include "Renderer/SlangGraphicsContext.h"
#include "Scene/TriangleMesh.h"

namespace krd {
class RenderStaticTriangleMesh final : public RenderObject {
private:
    RenderStaticTriangleMesh(RenderScene *renderScene) : RenderObject(renderScene) {}

public:
    static Ref<RenderStaticTriangleMesh>
    create(RenderScene *renderScene, Ref<TriangleMesh> const &mesh) {
        return {new RenderStaticTriangleMesh(renderScene)};
    }
};
} // namespace krd
