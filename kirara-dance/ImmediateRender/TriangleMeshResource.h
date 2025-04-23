#pragma once

#include "SceneGraph/Node.h"
#include "SlangGraphicsContext.h"

namespace krd {
///
///
///
class TriangleMeshResource final : public Node {
public:
    struct DeviceData {
        ComPtr<gfx::IBufferResource> vertexBuffer;
        ComPtr<gfx::IBufferResource> indexBuffer;
    };

    [[nodiscard]] static Ref<TriangleMeshResource> create() { return {new TriangleMeshResource}; }

    ~TriangleMeshResource() override = default;

    void accept(Visitor &visitor) override { visitor.apply(*this); }
    void accept(ConstVisitor &visitor) const override { visitor.apply(*this); }

private:
    Ref<DeviceData> const mesh;
};
} // namespace krd
