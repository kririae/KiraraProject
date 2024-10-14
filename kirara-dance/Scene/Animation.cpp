#include "Scene/Animation.h"

#include <algorithm>

#include "Scene/SceneGraph.h"

namespace krd {
void SceneNodeAnimation::doAnim(float curTime) {
    if (!sceneNode)
        return;
    auto const transform = getTransformationAtTime(curTime);
    sceneNode->setLocalTransform(transform);
}

Transform SceneNodeAnimation::getTransformationAtTime(float time) const {
    KRD_ASSERT(sceneNode, "Transform: sceneNode should not be empty");
    auto defaultTransform = sceneNode->getLocalTransform();
    float3 const translation =
        translationSeq.getAtTime(time, preState, postState, defaultTransform.getTranslation());
    float4 const rotation =
        rotationSeq.getAtTime<true>(time, preState, postState, defaultTransform.getRotation());
    float3 const scaling =
        scalingSeq.getAtTime(time, preState, postState, defaultTransform.getScaling());
    return {translation, rotation, scaling};
}

void SceneNodeAnimation::sortSeq() {
    std::stable_sort(translationSeq.begin(), translationSeq.end());
    std::stable_sort(rotationSeq.begin(), rotationSeq.end());
    std::stable_sort(scalingSeq.begin(), scalingSeq.end());
}

void Animation::resetAnimation() {
    for (auto const &snAnim : snAnims)
        snAnim->doAnim(0);
    curTime = 0;
}

void Animation::tick(float deltaTime) {
    for (auto const &snAnim : snAnims)
        snAnim->doAnim(curTime * 1000 /* ms */);
    curTime += deltaTime;
}
} // namespace krd
