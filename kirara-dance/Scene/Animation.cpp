#include "Animation.h"

#include "Scene/Transform.h"
#include "SceneGraph/Visitors/ExtractTypeOf.h"

namespace krd {
void TransformAnimationChannel::doAnim(float curTime) {
    if (!transform)
        return;
    float3 const translation =
        translationSeq.getAtTime(curTime, preState, postState, transform->getTranslation());
    float4 const rotation =
        rotationSeq.getAtTime<true>(curTime, preState, postState, transform->getRotation());
    float3 const scaling =
        scalingSeq.getAtTime(curTime, preState, postState, transform->getScaling());

    transform->setTranslation(translation);
    transform->setRotation(rotation);
    transform->setScaling(scaling);
}

void TransformAnimationChannel::sortSeq() {
    std::stable_sort(translationSeq.begin(), translationSeq.end());
    std::stable_sort(rotationSeq.begin(), rotationSeq.end());
    std::stable_sort(scalingSeq.begin(), scalingSeq.end());
}

void Animation::tick(float deltaTime) {
    ExtractTypeOf<TransformAnimationChannel> visitor;
    anims->accept(visitor);

    for (auto const &tAnim : visitor)
        tAnim->doAnim(curTime * 1000 /* ms */);
    curTime += deltaTime;
}
} // namespace krd
