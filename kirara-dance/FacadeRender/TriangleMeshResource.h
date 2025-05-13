#pragma once

#include "Scene/TriangleMesh.h"
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

public:
    /// Upload the mesh data to the GPU and record it into the member.
    void uploadTriMesh(TriangleMesh *triMesh, SlangGraphicsContext *context);
    ///
    std::shared_ptr<DeviceData> getDeviceData() const { return deviceData; }
    ///
    [[nodiscard]] auto getNumVertices() const { return numVertices; }
    ///
    [[nodiscard]] auto getNumIndices() const { return numIndices; }

private:
    uint64_t numVertices{0}, numIndices{0};
    std::shared_ptr<DeviceData> deviceData;
};
} // namespace krd
