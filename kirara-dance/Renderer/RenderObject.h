#pragma once

#include "Core/Object.h"

namespace krd {
class RenderScene;
class Scene;
class SlangGraphicsContext;

/// Base class for an object that belongs to a render scene, e.g., RenderCamera, RenderLight, etc.
class RenderObject : public Object {
public:
    ~RenderObject() override = default;

    /// Get the render scene that this object belongs to.
    [[nodiscard]] RenderScene *getRenderScene() const {
        KRD_ASSERT(renderScene, "RenderObject: RenderScene is not set");
        return renderScene;
    }

    /// Get the non-zero unique identifier of this object in the render scene.
    [[nodiscard]] uint64_t getRenderSceneId() const {
        KRD_ASSERT(renderSceneId != 0, "RenderObject: Render id is not set");
        return renderSceneId;
    }

    /// Get the scene that the render scene observes.
    [[nodiscard]] Scene *getScene() const;

    /// Get the graphics context that the render scene is associated with.
    [[nodiscard]] SlangGraphicsContext *getGraphicsContext() const;

    /// Try to pull the object from the scene.
    virtual bool pull() { return false; }

protected:
    explicit RenderObject(RenderScene *renderScene);

private:
    ///
    RenderScene *renderScene;
    ///
    uint64_t const renderSceneId;
};
} // namespace krd
