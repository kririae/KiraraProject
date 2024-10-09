#pragma once

#include "FLux/RenderObject.h"
#include "FLux/SlangGraphicsContext.h"
#include "Scene/TriangleMesh.h"

namespace krd {
/// The vertex's layout accepted by the shader.
struct Vertex {
    float position[3];
    float normal[3];
};

class RenderTriangleMesh final : public RenderObject {
    RenderTriangleMesh(FLuxScene *renderScene, Ref<TriangleMesh> const &mesh);

public:
    ///
    static Ref<RenderTriangleMesh> create(FLuxScene *renderScene, Ref<TriangleMesh> const &mesh) {
        return {new RenderTriangleMesh(renderScene, mesh)};
    }

    /// \see RenderObject::pull
    bool pull() override;

    struct Resource {
        gfx::IBufferResource *vertexBuffer;
        gfx::IBufferResource *indexBuffer;
    };

    /// Get the resource of the triangle mesh.
    [[nodiscard]] std::shared_ptr<Resource> getResource();

    ///
    [[nodiscard]] auto getNumVertices() const { return vertices.size(); }

    ///
    [[nodiscard]] auto getNumIndices() const { return indices.size(); }

private:
    /// A handler to the original triangle mesh.
    Ref<TriangleMesh> const mesh;

    // "transposed" data from TriangleMesh to RenderTriangleMesh
    kira::SmallVector<Vertex> vertices;
    kira::SmallVector<uint32_t> indices;

    ComPtr<gfx::IBufferResource> vertexBuffer;
    ComPtr<gfx::IBufferResource> indexBuffer;
};
} // namespace krd
