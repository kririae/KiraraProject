#include "Renderer/RenderTriangleMesh.h"

#include "Renderer/RenderScene.h"

namespace krd {
RenderTriangleMesh::RenderTriangleMesh(RenderScene *renderScene, Ref<TriangleMesh> const &mesh)
    : RenderObject(renderScene), mesh(mesh) {
    KRD_ASSERT(renderScene->getScene() == mesh->getScene());
}

bool RenderTriangleMesh::pull() {
    vertices.clear();
    indices.clear();

    auto const &oVertices = mesh->getVertices();
    auto const &oNormals = mesh->getNormals();
    auto const &oFaces = mesh->getFaces();

    LogInfo(
        "RenderTriangleMesh: pulling {:d} vertices, {:d} faces...", oVertices.rows(), oFaces.rows()
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

std::shared_ptr<RenderTriangleMesh::Resource> RenderTriangleMesh::getResource() {
    auto const *ctx = getGraphicsContext();

    gfx::IBufferResource::Desc vertexBufferDesc;
    vertexBufferDesc.type = gfx::IResource::Type::Buffer;
    vertexBufferDesc.sizeInBytes = vertices.size() * sizeof(Vertex);
    vertexBufferDesc.defaultState = gfx::ResourceState::VertexBuffer;
    ctx->getDevice()->createBufferResource(
        vertexBufferDesc, vertices.data(), vertexBuffer.writeRef()
    );

    gfx::IBufferResource::Desc indexBufferDesc;
    indexBufferDesc.type = gfx::IResource::Type::Buffer;
    indexBufferDesc.sizeInBytes = indices.size() * sizeof(uint32_t);
    indexBufferDesc.defaultState = gfx::ResourceState::IndexBuffer;
    ctx->getDevice()->createBufferResource(indexBufferDesc, indices.data(), indexBuffer.writeRef());

    auto resource = std::make_shared<Resource>();
    resource->vertexBuffer = vertexBuffer.get();
    resource->indexBuffer = indexBuffer.get();

    return resource;
}
} // namespace krd
