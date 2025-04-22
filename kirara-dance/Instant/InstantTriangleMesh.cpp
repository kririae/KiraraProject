#include "Instant/InstantTriangleMesh.h"

#include "Instant/InstantScene.h"

namespace krd {
InstantTriangleMesh::InstantTriangleMesh(InstantScene *instantScene, Ref<TriangleMesh> const &mesh)
    : InstantObject(instantScene), mesh(mesh) {
    KRD_ASSERT(instantScene->getScene() == mesh->getScene());
}

bool InstantTriangleMesh::pull() {
#if 0
    auto mMatrix = mesh->getGlobalTransformMatrix();
    auto itMatrix = transpose(inverse(mMatrix));

    mMatrix = transpose(mMatrix);
    std::memcpy(modelMatrix.data(), &mMatrix, sizeof(modelMatrix));
    itMatrix = transpose(itMatrix); // for consistency
    std::memcpy(
        inverseTransposedModelMatrix.data(), &itMatrix, sizeof(inverseTransposedModelMatrix)
    );
#endif

    if (!vertices.empty())
        return false;

    vertices.clear();
    indices.clear();

    auto const &oVertices = mesh->getVertices();
    auto const &oNormals = mesh->getNormals();
    auto const &oFaces = mesh->getFaces();

    LogInfo(
        "InstantTriangleMesh: pulling {:d} vertices, {:d} faces...", oVertices.rows(), oFaces.rows()
    );

    KRD_ASSERT(oVertices.rows() == mesh->getNumVertices());
    KRD_ASSERT(oVertices.rows() == oNormals.rows());

    vertices.resize(mesh->getNumVertices());
    indices.resize(mesh->getNumFaces() * 3);

    for (long i = 0; i < mesh->getNumVertices(); ++i) {
        auto const &oVertex = oVertices.row(i);
        auto const &oNormal = oNormals.row(i);

        vertices[i].position[0] = oVertex[0];
        vertices[i].position[1] = oVertex[1];
        vertices[i].position[2] = oVertex[2];

        vertices[i].normal[0] = oNormal[0];
        vertices[i].normal[1] = oNormal[1];
        vertices[i].normal[2] = oNormal[2];
    }

    for (long i = 0; i < mesh->getNumFaces(); ++i) {
        auto const &oFace = oFaces.row(i);

        indices[i * 3 + 0] = oFace[0];
        indices[i * 3 + 1] = oFace[1];
        indices[i * 3 + 2] = oFace[2];
    }

    return true;
}

Ref<InstantTriangleMesh::DeviceData> InstantTriangleMesh::upload(SlangGraphicsContext *context) {
    if (deviceData)
        return deviceData;

    deviceData = make_ref<DeviceData>();
    gfx::IBufferResource::Desc vertexBufferDesc;
    vertexBufferDesc.type = gfx::IResource::Type::Buffer;
    vertexBufferDesc.sizeInBytes = vertices.size() * sizeof(Vertex);
    vertexBufferDesc.defaultState = gfx::ResourceState::VertexBuffer;
    context->getDevice()->createBufferResource(
        vertexBufferDesc, vertices.data(), deviceData->vertexBuffer.writeRef()
    );

    gfx::IBufferResource::Desc indexBufferDesc;
    indexBufferDesc.type = gfx::IResource::Type::Buffer;
    indexBufferDesc.sizeInBytes = indices.size() * sizeof(uint32_t);
    indexBufferDesc.defaultState = gfx::ResourceState::IndexBuffer;
    context->getDevice()->createBufferResource(
        indexBufferDesc, indices.data(), deviceData->indexBuffer.writeRef()
    );

    return deviceData;
}
} // namespace krd
