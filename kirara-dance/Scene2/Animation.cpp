#include "Animation.h"

#include "Scene2/Transform.h"

namespace krd {
void TransformAnimationChannel::doAnim(float curTime) {}

#if 0
Transform TransformAnimationChannel::getTransformationAtTime(float time) const {
    KRD_ASSERT(transform, "Transform: transform should not be empty");
    auto defaultTransform = transform;
    float3 const translation =
        translationSeq.getAtTime(time, preState, postState, defaultTransform->getTranslation());
    float4 const rotation =
        rotationSeq.getAtTime<true>(time, preState, postState, defaultTransform->getRotation());
    float3 const scaling =
        scalingSeq.getAtTime(time, preState, postState, defaultTransform->getScaling());
    return {translation, rotation, scaling};
}
#endif

void TransformAnimationChannel::sortSeq() {
    std::stable_sort(translationSeq.begin(), translationSeq.end());
    std::stable_sort(rotationSeq.begin(), rotationSeq.end());
    std::stable_sort(scalingSeq.begin(), scalingSeq.end());
}

void Animation::resetAnimation() {}

void Animation::tick(float deltaTime) {}
} // namespace krd
