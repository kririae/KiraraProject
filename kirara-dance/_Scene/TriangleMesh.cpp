#include "Scene/TriangleMesh.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <igl/per_vertex_normals.h>

#include <assimp/Importer.hpp>

#include "Scene/Scene.h"

namespace krd {
TriangleMesh::TriangleMesh(Scene *scene) : SceneObject(scene) {}

void TriangleMesh::loadFromAssimp(aiMesh const *mesh, std::string_view name) {
    // Validate the `inMesh`
    if (!mesh->HasPositions() || !mesh->HasFaces())
        throw kira::Anyhow(
            "TriangleMesh: The mesh '{:s}' does not have positions or faces.", mesh->mName.C_Str()
        );

    if (name.empty())
        name = mesh->mName.C_Str();

    V.resize(mesh->mNumVertices, 3);
    F.resize(mesh->mNumFaces, 3);

    LogTrace("TriangleMesh: Loading mesh '{:s}'...", name);
    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
        V.row(i) << mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z;
    LogTrace("TriangleMesh: Loaded {:d} vertices", mesh->mNumVertices);

    for (uint32_t i = 0; i < mesh->mNumFaces; ++i) {
        aiFace const &face = mesh->mFaces[i];
        if (face.mNumIndices != 3)
            throw kira::Anyhow("TriangleMesh: Only triangle faces are supported in '{:s}'", name);
        F.row(i) << face.mIndices[0], face.mIndices[1], face.mIndices[2];
    }
    LogTrace("TriangleMesh: Loaded {:d} faces", mesh->mNumFaces);

    if (mesh->HasNormals()) {
        N.resize(mesh->mNumVertices, 3);
        for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
            N.row(i) << mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z;
        LogTrace("TriangleMesh: Loaded {:d} normals", mesh->mNumVertices);
    } else {
        LogTrace("TriangleMesh: No normals found in '{:s}'", name);
        this->calculateNormal();
    }

    LogInfo(
        "TriangleMesh: Loaded '{:s}' with {:d} vertices and {:d} faces", name, mesh->mNumVertices,
        mesh->mNumFaces
    );
}

void TriangleMesh::loadFromFile(std::filesystem::path const &path) {
    // \c libigl can just load V and F, and is not that robust in loading mesh with attributes.
    Assimp::Importer importer;

    aiScene const *scene = importer.ReadFile(path.string(), aiProcess_Triangulate);
    if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        throw kira::Anyhow(
            "TriangleMesh: Failed to load the model from '{:s}': {:s}", path.string(),
            importer.GetErrorString()
        );
    if (scene->mNumMeshes != 1)
        throw kira::Anyhow(
            "TriangleMesh: Exactly one mesh is expected in '{:s}', it has {:d}", path.string(),
            scene->mNumMeshes
        );

    aiMesh const *mesh = scene->mMeshes[0];
    loadFromAssimp(mesh, path.string());
}

void TriangleMesh::calculateNormal(TriangleMesh::NormalWeightingType weighting) {
    igl::PerVertexNormalsWeightingType iglWeighting{};
    switch (weighting) {
    case NormalWeightingType::ByArea:
        iglWeighting = igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_AREA;
        break;
    case NormalWeightingType::ByAngle:
        iglWeighting = igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_ANGLE;
        break;
    default: KRD_ASSERT(false);
    }

    LogTrace(
        "TriangleMesh: Calculating normals on {:d} vertices and {:d} faces with {:s} weighting...",
        getNumVertices(), getNumFaces(), weighting == NormalWeightingType::ByArea ? "area" : "angle"
    );
    igl::per_vertex_normals(V, F, iglWeighting, N);
}
} // namespace krd
