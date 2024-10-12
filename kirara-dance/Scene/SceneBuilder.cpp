#include "Scene/SceneBuilder.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <iostream>

#include "Scene/Scene.h"
#include "Scene/TriangleMesh.h"

namespace krd {
namespace {
using TriangleMap = std::unordered_map<
    /* index in the original array */ uint32_t, /* pointer to the scene */ TriangleMesh *>;

Ref<SceneNode> addToSceneGraph(
    SceneGraph &sceneGraph, TriangleMap const &triMap, aiNode const *node, SceneNode *parent
) {
    aiVector3t<float> scaling, position;
    aiQuaterniont<float> rotation;
    node->mTransformation.Decompose(scaling, rotation, position);

    Transform transform;
    transform.setScaling(float3{scaling.x, scaling.y, scaling.z});
    transform.setRotation(float4{rotation.x, rotation.y, rotation.z, rotation.w});
    transform.setTranslation(float3{position.x, position.y, position.z});

    auto sceneNode = make_ref<SceneNode>(node->mName.C_Str(), transform, parent);
    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        auto children = addToSceneGraph(sceneGraph, triMap, node->mChildren[i], sceneNode.get());
        sceneNode->addChild(children);
    }

    // For the special case where the node has meshes, attach the meshes to the scene node.
    for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        auto *triMesh = triMap.at(node->mMeshes[i]);
        triMesh->attachToSceneNode(sceneNode.get());
    }

    return sceneNode;
}
} // namespace

void SceneBuilder::loadFromFile(std::filesystem::path const &path) {
    if (scene)
        scene.reset();
    scene = Scene::create({});
    scene->create<TriangleMesh>(R"(C:\Users\kriae\Projects\flux-data\cbox\geometry\floor.ply)");

    Assimp::Importer importer;

    aiScene const *aiScene = importer.ReadFile(path.string(), aiProcess_Triangulate);
    if (!aiScene || !aiScene->mRootNode || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        throw kira::Anyhow(
            "SceneBuilder: Failed to load the scene from '{:s}': {:s}", path.string(),
            importer.GetErrorString()
        );

    // Load
    // - (x) mMeshes
    // - mMaterials
    // - mAnimations
    // - mTextures
    // - mLights
    // - mCameras
    // - mSkeletons
    //
    // Which are structured in the scene graph, i.e., nodes.
    TriangleMap triMap;
    for (uint32_t meshId = 0; meshId < aiScene->mNumMeshes; ++meshId) {
        auto mesh = scene->create<TriangleMesh>();
        mesh->loadFromAssimp(aiScene->mMeshes[meshId], std::string_view{});

        triMap.emplace(meshId, mesh.get());
    }

    //
    scene->getSceneGraph().root =
        addToSceneGraph(scene->getSceneGraph(), triMap, aiScene->mRootNode, nullptr);
}
} // namespace krd
