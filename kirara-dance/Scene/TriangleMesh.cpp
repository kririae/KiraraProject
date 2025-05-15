#include "TriangleMesh.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <igl/per_vertex_normals.h>
#include <tbb/parallel_for.h>

#include <assimp/Importer.hpp>

#include "Core/detail/Linalg.h"
#include "Scene/Visitors/ExtractRelativeTransforms.h"

namespace krd {
void TriangleMesh::loadFromAssimp(aiMesh const *inMesh, std::string_view name) {
    // Validate the `inMesh`
    if (!inMesh->HasPositions() || !inMesh->HasFaces())
        throw kira::Anyhow(
            "TriangleMesh: The mesh '{:s}' does not have positions or faces.", inMesh->mName.C_Str()
        );

    if (name.empty())
        name = inMesh->mName.C_Str();

    this->name = name;

    V.resize(inMesh->mNumVertices, 3);
    F.resize(inMesh->mNumFaces, 3);

    LogTrace("TriangleMesh: Loading mesh '{:s}'...", name);
    for (uint32_t i = 0; i < inMesh->mNumVertices; ++i)
        V.row(i) << inMesh->mVertices[i].x, inMesh->mVertices[i].y, inMesh->mVertices[i].z;
    LogTrace("TriangleMesh: Loaded {:d} vertices", inMesh->mNumVertices);

    for (uint32_t i = 0; i < inMesh->mNumFaces; ++i) {
        aiFace const &face = inMesh->mFaces[i];
        if (face.mNumIndices != 3)
            throw kira::Anyhow("TriangleMesh: Only triangle faces are supported in '{:s}'", name);
        F.row(i) << face.mIndices[0], face.mIndices[1], face.mIndices[2];
    }
    LogTrace("TriangleMesh: Loaded {:d} faces", inMesh->mNumFaces);

    if (inMesh->HasNormals()) {
        N.resize(inMesh->mNumVertices, 3);
        for (uint32_t i = 0; i < inMesh->mNumVertices; ++i)
            N.row(i) << inMesh->mNormals[i].x, inMesh->mNormals[i].y, inMesh->mNormals[i].z;
        LogTrace("TriangleMesh: Loaded {:d} normals", inMesh->mNumVertices);
    } else {
        LogTrace("TriangleMesh: No normals found in '{:s}'", name);
        this->calculateNormal();
    }

    LogInfo(
        "TriangleMesh: Loaded '{:s}' with {:d} vertices and {:d} faces", name, inMesh->mNumVertices,
        inMesh->mNumFaces
    );
}

void TriangleMesh::loadFromAssimp(
    aiMesh const *inMesh, std::string_view name,
    std::unordered_map<std::string, Node::UUIDType> const &transIdMap
) {
    loadFromAssimp(inMesh, name);

    if (inMesh->HasBones()) {
        // Load the bone weights.
        W.resize(inMesh->mNumVertices, inMesh->mNumBones);
        W.setZero();
        for (uint32_t i = 0; i < inMesh->mNumBones; ++i) {
            auto const *bone = inMesh->mBones[i];
            for (uint32_t j = 0; j < bone->mNumWeights; ++j) {
                auto const &weight = bone->mWeights[j];
                W(weight.mVertexId, i) = weight.mWeight;
            }
        }

        //
        nodeIds.resize(inMesh->mNumBones);
        rootNodeIds.resize(inMesh->mNumBones);
        for (uint32_t i = 0; i < inMesh->mNumBones; ++i) {
            auto const *bone = inMesh->mBones[i];

            // (1)
            auto it = transIdMap.find(bone->mName.C_Str());
            if (it == transIdMap.end())
                throw kira::Anyhow(
                    "TriangleMesh: The bone '{:s}' is not found in the transform ID map",
                    bone->mName.C_Str()
                );
            nodeIds[i] = it->second;

            // (2)
            KRD_ASSERT(
                bone->mArmature,
                "The bone '{:s}' has no a valid armature, considering turnning on  "
                "aiProcess_PopulateArmatureData",
                bone->mName.C_Str()
            );
            auto it2 = transIdMap.find(bone->mArmature->mName.C_Str());
            if (it2 == transIdMap.end())
                throw kira::Anyhow(
                    "TriangleMesh: The armature '{:s}' is not found in the transform ID map",
                    bone->mArmature->mName.C_Str()
                );
            rootNodeIds[i] = it2->second;
        }

        // Load the inverse bind matrices.
        inverseBindMatrices.resize(inMesh->mNumBones);
        for (uint32_t i = 0; i < inMesh->mNumBones; ++i) {
            auto const *bone = inMesh->mBones[i];
            auto inIBM = bone->mOffsetMatrix;
            inverseBindMatrices[i] = float4x4{
                float4{inIBM.a1, inIBM.b1, inIBM.c1, inIBM.d1},
                float4{inIBM.a2, inIBM.b2, inIBM.c2, inIBM.d2},
                float4{inIBM.a3, inIBM.b3, inIBM.c3, inIBM.d3},
                float4{inIBM.a4, inIBM.b4, inIBM.c4, inIBM.d4},
            };
        }
        LogTrace("TriangleMesh: Loaded {:d} bones", inMesh->mNumBones);
    } else {
        LogWarn("TriangleMesh: No bones found in '{:s}'", this->name);
    }
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

Ref<TriangleMesh> TriangleMesh::adaptLinearBlendSkinning(Ref<Node> const &root) const {
    // Extract the transforms
    ExtractRelativeTransforms::Desc desc{.rootNodeIds = rootNodeIds, .nodeIds = nodeIds};
    ExtractRelativeTransforms eNodeTrans(desc);
    root->accept(eNodeTrans);

    // Create a new mesh with the same vertices and faces
    auto newMesh = TriangleMesh::create();
    newMesh->V.resize(V.rows(), V.cols());
    newMesh->F = F;

    kira::SmallVector<float4x4> boneTransforms;
    boneTransforms.reserve(W.cols());
    for (int j = 0; j < W.cols(); ++j) {
        auto it = eNodeTrans.find(nodeIds[j]);
        if (it == eNodeTrans.end())
            throw kira::Anyhow(
                "TriangleMesh: The node ID {:d} is not found in the transform map", nodeIds[j]
            );
        boneTransforms.emplace_back(it->second);
    }

    tbb::parallel_for(
        tbb::blocked_range<long>(0, newMesh->V.rows()),
        [&](tbb::blocked_range<long> const &range) {
        for (auto i = range.begin(); i != range.end(); ++i) {
            auto const eVtx = V.row(i);
            auto vtx = float4{eVtx.x(), eVtx.y(), eVtx.z(), 1.0f};
            auto res = float3{0.0f, 0.0f, 0.0f};
            for (long j = 0; j < W.cols(); ++j) {
                auto inc = mul(boneTransforms[j], mul(inverseBindMatrices[j], vtx));
                inc.xyz() /= inc.w;
                res += W(i, j) * inc.xyz();
            }

            newMesh->V(i, 0) = res.x;
            newMesh->V(i, 1) = res.y;
            newMesh->V(i, 2) = res.z;
        }
    }
    );

    newMesh->calculateNormal();
    return newMesh;
}
} // namespace krd
