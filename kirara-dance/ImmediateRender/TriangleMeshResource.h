#pragma once

#include "Scene2/TriangleMesh.h"
#include "SceneGraph/Node.h"
#include "SceneGraph/NodeMixin.h"
#include "SlangGraphicsContext.h"

namespace krd {
struct Vertex {
    float position[3];
    float normal[3];
};

///
///
///
class TriangleMeshResource final : public NodeMixin<TriangleMeshResource, Node> {
public:
    struct DeviceData {
        ComPtr<gfx::IBufferResource> vertexBuffer;
        ComPtr<gfx::IBufferResource> indexBuffer;
    };

    [[nodiscard]] static Ref<TriangleMeshResource> create() { return {new TriangleMeshResource}; }
    ~TriangleMeshResource() override = default;

    /// Upload the mesh data to the GPU and record it into the member.
    void upload(TriangleMesh *triMesh, SlangGraphicsContext *context);

private:
    std::shared_ptr<DeviceData> deviceData;
};
} // namespace krd
