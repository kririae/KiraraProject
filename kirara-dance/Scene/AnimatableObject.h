#pragma once

#include "Scene/SceneGraph.h"
#include "Scene/SceneObject.h"

namespace krd {
///
///
/// \remark In \c AnimatableObject, we don't have to further enforce compile-time check to attach a
/// \c SceneNode to the object.
class AnimatableObject : public SceneObject {
public:
    using SceneObject::SceneObject;

    ///
    void attachToSceneNode(SceneNode *node) { sceneNode = node; }
    ///
    [[nodiscard]] SceneNode *getSceneNode() const { return sceneNode; }

    /// \see SceneObject::isAnimatable
    [[nodiscard]] bool isAnimatable() const override { return true; }

protected:
    explicit AnimatableObject(Scene *scene) : SceneObject(scene) {}

private:
    SceneNode *sceneNode{nullptr};
};
} // namespace krd
