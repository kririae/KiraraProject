#include "Scene/SceneBuilder.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>

#include "Scene/Animation.h"
#include "Scene/Scene.h"
#include "Scene/TriangleMesh.h"

namespace krd {
namespace {
using TriangleMeshMap = std::unordered_map<
    /* index in the original array */ uint32_t, /* pointer to the mesh */ TriangleMesh *>;
using NodeMap = std::unordered_map<
    /* node name */ std::string, /* pointer to the node */ SceneNode *>;

Ref<SceneNode> addToSceneGraph(
    SceneGraph &sceneGraph, TriangleMeshMap const &triMap, NodeMap &nodeMap, aiNode const *node,
    SceneNode *parent
) {
    aiVector3t<float> scaling, position;
    aiQuaterniont<float> rotation;
    node->mTransformation.Decompose(scaling, rotation, position);

    Transform transform;
    transform.setScaling(float3{scaling.x, scaling.y, scaling.z});
    transform.setRotation(float4{rotation.x, rotation.y, rotation.z, rotation.w});
    transform.setTranslation(float3{position.x, position.y, position.z});

    auto sceneNode = SceneNode::create(node->mName.C_Str(), transform, parent);
    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        auto children =
            addToSceneGraph(sceneGraph, triMap, nodeMap, node->mChildren[i], sceneNode.get());
        sceneNode->addChild(children);
    }

    // For the special case where the node has meshes, attach the meshes to the scene node.
    for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        auto *triMesh = triMap.at(node->mMeshes[i]);
        triMesh->attachToSceneNode(sceneNode);
    }

    nodeMap[node->mName.C_Str()] = sceneNode.get();
    return sceneNode;
}

void loadFromAnimChannel(SceneNodeAnimation *snAnim, aiNodeAnim const *nodeAnim) {
    AnimationSequence<float3> tSeq;
    AnimationSequence<float4> rSeq;
    AnimationSequence<float3> sSeq;
    kira::SmallVector<AnimationKey<float3>> tKeys;
    kira::SmallVector<AnimationKey<float4>> rKeys;
    kira::SmallVector<AnimationKey<float3>> sKeys;

    tSeq.reserve(nodeAnim->mNumPositionKeys);
    rSeq.reserve(nodeAnim->mNumRotationKeys);
    sSeq.reserve(nodeAnim->mNumScalingKeys);

    for (uint32_t i = 0; i < nodeAnim->mNumPositionKeys; ++i) {
        auto const &key = nodeAnim->mPositionKeys[i];
        tSeq.push_back(
            {.time = key.mTime, .value = float3{key.mValue.x, key.mValue.y, key.mValue.z}}
        );
    }

    for (uint32_t i = 0; i < nodeAnim->mNumRotationKeys; ++i) {
        auto const &key = nodeAnim->mRotationKeys[i];
        rSeq.push_back(
            {.time = key.mTime,
             .value = float4{key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}}
        );
    }

    for (uint32_t i = 0; i < nodeAnim->mNumScalingKeys; ++i) {
        auto const &key = nodeAnim->mScalingKeys[i];
        sSeq.push_back(
            {.time = key.mTime, .value = float3{key.mValue.x, key.mValue.y, key.mValue.z}}
        );
    }

    snAnim->setTranslationSeq(std::move(tSeq));
    snAnim->setRotationSeq(std::move(rSeq));
    snAnim->setScalingSeq(std::move(sSeq));
    snAnim->sortSeq();

    auto aiAnimBehaviourToLocal = [](aiAnimBehaviour const &behaviour) -> AnimationBehaviour {
        using enum AnimationBehaviour;
        switch (behaviour) {
        case aiAnimBehaviour_DEFAULT: return Default;
        case aiAnimBehaviour_CONSTANT: return Constant;
        case aiAnimBehaviour_LINEAR: return Linear;
        case aiAnimBehaviour_REPEAT: return Repeat;
        default: return Default;
        }
    };

    snAnim->setPreState(aiAnimBehaviourToLocal(nodeAnim->mPreState));
    snAnim->setPostState(aiAnimBehaviourToLocal(nodeAnim->mPostState));
}
} // namespace

void SceneBuilder::loadFromFile(std::filesystem::path const &path) {
    if (scene)
        scene.reset();
    scene = Scene::create({});

    Assimp::Importer importer;

    aiScene const *aiScene = importer.ReadFile(path.string(), aiProcess_Triangulate);
    if (!aiScene || !aiScene->mRootNode || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        throw kira::Anyhow(
            "SceneBuilder: Failed to load the scene from \"{:s}\": {:s}", path.string(),
            importer.GetErrorString()
        );

    // Load
    // - (x) mMeshes
    // - mMaterials
    // - (partially) mAnimations
    // - mTextures
    // - mLights
    // - mCameras
    // - mSkeletons
    //
    // Which are structured in the scene graph, in other words, nodes.
    TriangleMeshMap triMap;
    for (uint32_t meshId = 0; meshId < aiScene->mNumMeshes; ++meshId) {
        auto mesh = scene->create<TriangleMesh>();
        mesh->loadFromAssimp(aiScene->mMeshes[meshId], std::string_view{});

        triMap.emplace(meshId, mesh.get());
    }

    //
    NodeMap nodeMap;
    scene->getSceneGraph().root =
        addToSceneGraph(scene->getSceneGraph(), triMap, nodeMap, aiScene->mRootNode, nullptr);

    //
    for (uint32_t animId = 0; animId < aiScene->mNumAnimations; ++animId) {
        auto const *aiAnim = aiScene->mAnimations[animId];
        auto const anim = scene->create<Animation>();

        if (aiAnim->mName.length == 0)
            LogInfo("SceneBuilder: Loading animation with {:d} channels...", aiAnim->mNumChannels);
        else
            LogInfo(
                "SceneBuilder: Loading animation '{:s}' with {:d} channels...",
                aiAnim->mName.C_Str(), aiAnim->mNumChannels
            );

        for (uint32_t channelId = 0; channelId < aiAnim->mNumChannels; ++channelId) {
            auto const *channel = aiAnim->mChannels[channelId];
            auto *node = nodeMap[channel->mNodeName.C_Str()];

            auto snAnim = SceneNodeAnimation::create();
            snAnim->bindSceneNode(node);
            loadFromAnimChannel(snAnim.get(), channel);

            // Add the channel into the animation instance.
            anim->addChannel(std::move(snAnim));
        }
    }

    LogInfo("SceneBuilder: Scene is built");
}
} // namespace krd
