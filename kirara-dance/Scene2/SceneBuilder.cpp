#include "SceneBuilder.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <utility>

#include "Animation.h"
#include "Geometry.h"
#include "SceneRoot.h"
#include "Transform.h"
#include "TriangleMesh.h"

namespace krd {
namespace {
using TriangleMeshMap = std::unordered_map<
    /* index in the original array */ uint32_t, /* pointer to the mesh */ Ref<TriangleMesh>>;
using TransformMap = std::unordered_map<
    /* node name */ std::string, /* pointer to the node */ Transform *>;

void loadFromAnimChannel(TransformAnimationChannel *snAnim, aiNodeAnim const *nodeAnim) {
    AnimationSequence<float3> tSeq;
    AnimationSequence<float4> rSeq;
    AnimationSequence<float3> sSeq;
    kira::SmallVector<AnimationKey<float3>> tKeys;
    kira::SmallVector<AnimationKey<float4>> rKeys;
    kira::SmallVector<AnimationKey<float3>> sKeys;

    tSeq.reserve(nodeAnim->mNumPositionKeys);
    rSeq.reserve(nodeAnim->mNumRotationKeys);
    sSeq.reserve(nodeAnim->mNumScalingKeys);

    auto aiAnimInterpolationToLocal = [](aiAnimInterpolation const &interp
                                      ) -> AnimationInterpolation {
        using enum AnimationInterpolation;
        switch (interp) {
        case aiAnimInterpolation_Step: return Step;
        case aiAnimInterpolation_Linear: return Linear;
        case aiAnimInterpolation_Spherical_Linear: return SphericalLinear;
        case aiAnimInterpolation_Cubic_Spline: return CubicSpline;
        default: return Linear;
        }
    };

    for (uint32_t i = 0; i < nodeAnim->mNumPositionKeys; ++i) {
        auto const &key = nodeAnim->mPositionKeys[i];
        tSeq.push_back(
            {.time = key.mTime,
             .value = float3{key.mValue.x, key.mValue.y, key.mValue.z},
             .interp = aiAnimInterpolationToLocal(key.mInterpolation)}
        );
    }

    for (uint32_t i = 0; i < nodeAnim->mNumRotationKeys; ++i) {
        auto const &key = nodeAnim->mRotationKeys[i];
        rSeq.push_back(
            {.time = key.mTime,
             .value = float4{key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w},
             .interp = aiAnimInterpolationToLocal(key.mInterpolation)}
        );
    }

    for (uint32_t i = 0; i < nodeAnim->mNumScalingKeys; ++i) {
        auto const &key = nodeAnim->mScalingKeys[i];
        sSeq.push_back(
            {.time = key.mTime,
             .value = float3{key.mValue.x, key.mValue.y, key.mValue.z},
             .interp = aiAnimInterpolationToLocal(key.mInterpolation)}
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

/// Visitor to insert geometries into the scene graph.
///
class InsertFromAssimp : public Visitor {
    explicit InsertFromAssimp(TriangleMeshMap triMap, aiNode const *node)
        : triMap(std::move(triMap)), node(node) {}

public:
    [[nodiscard]] static Ref<InsertFromAssimp> create(TriangleMeshMap triMap, aiNode const *node) {
        return {new InsertFromAssimp(std::move(triMap), node)};
    }

    void apply(SceneRoot &sceneRoot) override {
        sceneRoot.getGeomGroup()->addChild(addToSceneGraph(this->triMap, this->node, tMap));
        for (auto &[_, triMesh] : triMap)
            sceneRoot.getMeshGroup()->addChild(triMesh);
    }

    /// Examine the recorded transform map.
    [[nodiscard]] auto &getTransformMap() { return tMap; }

private:
    TriangleMeshMap triMap;
    aiNode const *node;

    TransformMap tMap;

    static Ref<Transform>
    addToSceneGraph(TriangleMeshMap const &triMap, aiNode const *node, TransformMap &tMap) {
        aiVector3t<float> scaling, position;
        aiQuaterniont<float> rotation;
        node->mTransformation.Decompose(scaling, rotation, position);

        Ref<Transform> transform;
        if (node->mNumMeshes > 0) {
            auto geom = Geometry::create();
            // For the special case where the node has meshes, attach the meshes to the scene node.
            for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
                auto const &triMesh = triMap.at(node->mMeshes[i]);
                geom->linkMesh(triMesh);
            }

            transform = geom;
        } else
            transform = Transform::create();

        transform->setScaling(float3{scaling.x, scaling.y, scaling.z});
        transform->setRotation(float4{rotation.x, rotation.y, rotation.z, rotation.w});
        transform->setTranslation(float3{position.x, position.y, position.z});

        for (uint32_t i = 0; i < node->mNumChildren; ++i) {
            auto children = addToSceneGraph(triMap, node->mChildren[i], tMap);
            transform->addChild(children);
        }

        tMap.emplace(node->mName.C_Str(), transform.get());
        return transform;
    }
};
} // namespace

SceneBuilder::SceneBuilder() : sceneRoot(SceneRoot::create()) {}

void SceneBuilder::loadFromFile(std::filesystem::path const &path) {
    if (sceneRoot)
        sceneRoot.reset();
    sceneRoot = SceneRoot::create();

    Assimp::Importer importer;

    aiScene const *aiScene = importer.ReadFile(path.string(), aiProcess_Triangulate);
    if (!aiScene || !aiScene->mRootNode || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        throw kira::Anyhow(
            "SceneBuilder: Failed to load the scene from \"{:s}\": {:s}", path.string(),
            importer.GetErrorString()
        );

    // Load
    // - (partially) mMeshes
    // - mMaterials
    // - mAnimations
    // - mTextures
    // - mLights
    // - mCameras
    // - mSkeletons
    //
    // Which are structured in the scene graph, in other words, nodes.
    TriangleMeshMap triMap;
    for (uint32_t meshId = 0; meshId < aiScene->mNumMeshes; ++meshId) {
        auto mesh = TriangleMesh::create();
        mesh->loadFromAssimp(aiScene->mMeshes[meshId], std::string_view{});
        triMap.emplace(meshId, mesh.get());
    }

    LogInfo("Num meshes inserted: {:d}", aiScene->mNumMeshes);

    auto iFromAssimp = InsertFromAssimp::create(std::move(triMap), aiScene->mRootNode);
    sceneRoot->accept(*iFromAssimp);

    auto tMap = std::move(iFromAssimp->getTransformMap());

    //
    for (uint32_t animId = 0; animId < aiScene->mNumAnimations; ++animId) {
        auto const *aiAnim = aiScene->mAnimations[animId];
        auto const anim = Animation::create();

        if (aiAnim->mName.length == 0)
            LogInfo("SceneBuilder: Loading animation with {:d} channels...", aiAnim->mNumChannels);
        else
            LogInfo(
                "SceneBuilder: Loading animation '{:s}' with {:d} channels...",
                aiAnim->mName.C_Str(), aiAnim->mNumChannels
            );

        for (uint32_t channelId = 0; channelId < aiAnim->mNumChannels; ++channelId) {
            auto const *channel = aiAnim->mChannels[channelId];
            auto *node = tMap[channel->mNodeName.C_Str()];

            auto tAnim = TransformAnimationChannel::create();
            tAnim->bindTransform(node);
            loadFromAnimChannel(tAnim.get(), channel);

            // Add the channel into the animation instance.
            anim->addTransformChannel(std::move(tAnim));
        }

        if (aiAnim->mNumMeshChannels > 0) {
            LogWarn(
                "SceneBuilder: Mesh animation is not supported yet, skipping {:d} mesh channels",
                aiAnim->mNumMeshChannels
            );
        }

        if (aiAnim->mNumMorphMeshChannels > 0) {
            LogWarn(
                "SceneBuilder: Morph animation is not supported yet, skipping {:d} morph mesh "
                "channels",
                aiAnim->mNumMorphMeshChannels
            );
        }

        // Attach the animation to the scene root.
        sceneRoot->getAuxGroup()->addChild(anim);
    }

    LogTrace("SceneBuilder: Scene is built");
}
} // namespace krd
