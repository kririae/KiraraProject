#pragma once

#include "Instant/InstantObject.h"
#include "Instant/SlangGraphicsContext.h"
#include "Scene/TriangleMesh.h"

namespace krd {
/// The vertex's layout accepted by the shader.
struct Vertex {
    float position[3];
    float normal[3];
};

class InstantTriangleMesh final : public InstantObject {
    InstantTriangleMesh(InstantScene *instantScene, Ref<TriangleMesh> const &mesh);

public:
    /// The GPU proxy of the triangle mesh.
    struct DeviceData final : InstantScene::DeviceData {
        ComPtr<gfx::IBufferResource> vertexBuffer;
        ComPtr<gfx::IBufferResource> indexBuffer;
    };

    ///
    static Ref<InstantTriangleMesh>
    create(InstantScene *instantScene, Ref<TriangleMesh> const &mesh) {
        return {new InstantTriangleMesh(instantScene, mesh)};
    }

    /// \see InstantObject::pull
    bool pull() override;

    /// Get or create the device data on the \c SlangGraphicsContext.
    Ref<DeviceData> upload(SlangGraphicsContext *context);

    ///
    [[nodiscard]] float const *getModelMatrix() const { return modelMatrix.data(); }
    ///
    [[nodiscard]] float const *getInverseTransposedModelMatrix() const {
        return inverseTransposedModelMatrix.data();
    }

    ///
    [[nodiscard]] auto getNumVertices() const { return vertices.size(); }

    ///
    [[nodiscard]] auto getNumIndices() const { return indices.size(); }

private:
    /// A handler to the original triangle mesh.
    Ref<TriangleMesh> const mesh;

    /// The device data of the triangle mesh.
    Ref<DeviceData> deviceData;

    // "transposed" data from TriangleMesh to InstantTriangleMesh
    std::array<float, 16> modelMatrix{};
    std::array<float, 16> inverseTransposedModelMatrix{};
    kira::SmallVector<Vertex> vertices;
    kira::SmallVector<uint32_t> indices;
};
} // namespace krd
