#pragma once

#include "SceneGraph/Node.h"
#include "SceneGraph/NodeMixin.h"
#include "SlangGraphicsContext.h"

namespace krd {
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

private:
    Ref<DeviceData> const mesh;
};
} // namespace krd
