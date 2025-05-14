#include "TriMeshResource.h"

#include <tbb/parallel_for.h>

namespace krd {
void TriMeshResource::uploadTriMesh(TriangleMesh *triMesh, SlangGraphicsContext *context) {
    deviceData = std::make_shared<DeviceData>();

    // Transpose the vertex buffer into GPU-accepted mode
    kira::SmallVector<Vertex> vertices;
    kira::SmallVector<uint32_t> indices;

    auto const &oVertices = triMesh->getVertices();
    auto const &oNormals = triMesh->getNormals();
    auto const &oFaces = triMesh->getFaces();

    LogTrace(
        "TriMeshResource: pulling {:d} vertices, {:d} faces from {:s}...", oVertices.rows(),
        oFaces.rows(), triMesh->getHumanReadable()
    );

    KRD_ASSERT(oVertices.rows() == triMesh->getNumVertices());
    KRD_ASSERT(oVertices.rows() == oNormals.rows());

    vertices.resize(triMesh->getNumVertices());
    indices.resize(triMesh->getNumFaces() * 3);

    tbb::parallel_for(
        tbb::blocked_range<long>(0, triMesh->getNumVertices()),
        [&](tbb::blocked_range<long> const &r) {
        for (long i = r.begin(); i != r.end(); ++i) {
            auto const &oVertex = oVertices.row(i);
            auto const &oNormal = oNormals.row(i);

            vertices[i].position[0] = oVertex[0];
            vertices[i].position[1] = oVertex[1];
            vertices[i].position[2] = oVertex[2];

            vertices[i].normal[0] = oNormal[0];
            vertices[i].normal[1] = oNormal[1];
            vertices[i].normal[2] = oNormal[2];
        }
    }
    );

    tbb::parallel_for(
        tbb::blocked_range<long>(0, triMesh->getNumFaces()),
        [&](tbb::blocked_range<long> const &r) {
        for (long i = r.begin(); i != r.end(); ++i) {
            auto const &oFace = oFaces.row(i);

            indices[i * 3 + 0] = oFace[0];
            indices[i * 3 + 1] = oFace[1];
            indices[i * 3 + 2] = oFace[2];
        }
    }
    );

    //
    numVertices = triMesh->getNumVertices();
    numIndices = triMesh->getNumFaces() * 3;

    //
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
}
} // namespace krd
