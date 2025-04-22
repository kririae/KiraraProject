#include "Scene/SceneGraph.h"

namespace krd {
float4x4 SceneNode::getGlobalTransformMatrix() const {
    auto gMatrix = float4x4(identity);
    for (auto const *node = this; node != nullptr; node = node->getParent())
        gMatrix = mul(node->getLocalTransform().getMatrix(), gMatrix);
    return gMatrix;
}

float3 SceneNode::getGlobalPosition(float3 position) const {
    position = mul(getLocalTransform().getMatrix(), float4{position, 1.0f}).xyz();
    return position;
}
} // namespace krd
