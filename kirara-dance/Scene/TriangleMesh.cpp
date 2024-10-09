#include "Scene/TriangleMesh.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <igl/per_vertex_normals.h>

#include <assimp/Importer.hpp>

#include "Scene/Scene.h"

namespace krd {
TriangleMesh::TriangleMesh(Scene *scene) : SceneObject(scene) {}

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

    V.resize(mesh->mNumVertices, 3);
    F.resize(mesh->mNumFaces, 3);

    LogTrace("TriangleMesh: Loading '{:s}'...", path.string());
    for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
        V.row(i) << mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z;
    LogTrace("TriangleMesh: Loaded {:d} vertices", mesh->mNumVertices);

    if (mesh->HasNormals()) {
        N.resize(mesh->mNumVertices, 3);
        for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
            N.row(i) << mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z;
        LogTrace("TriangleMesh: Loaded {:d} normals", mesh->mNumVertices);
    } else {
        LogTrace("TriangleMesh: No normals found in '{:s}'", path.string());
        this->calculateNormal();
    }

    for (uint32_t i = 0; i < mesh->mNumFaces; ++i) {
        aiFace const &face = mesh->mFaces[i];
        if (face.mNumIndices != 3)
            throw kira::Anyhow(
                "TriangleMesh: Only triangle faces are supported in '{:s}'", path.string()
            );
        F.row(i) << face.mIndices[0], face.mIndices[1], face.mIndices[2];
    }
    LogTrace("TriangleMesh: Loaded {:d} faces", mesh->mNumFaces);

    LogInfo(
        "TriangleMesh: Loaded '{:s}' with {:d} vertices and {:d} faces", path.string(),
        mesh->mNumVertices, mesh->mNumFaces
    );
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
        "TriangleMesh: Calculating normals on {:d} vertices with {:s} weighting...",
        getNumVertices(), weighting == NormalWeightingType::ByArea ? "area" : "angle"
    );
    igl::per_vertex_normals(V, F, iglWeighting, N);
    LogTrace(
        "TriangleMesh: Calculated normals on {:d} vertices with {:s} weighting", getNumVertices(),
        weighting == NormalWeightingType::ByArea ? "area" : "angle"
    );
}
} // namespace krd
