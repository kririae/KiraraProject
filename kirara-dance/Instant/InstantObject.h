#pragma once

#include "Core/Object.h"

namespace krd {
class InstantScene;
class Scene;
class SlangGraphicsContext;

/// Base class for an object that belongs to a instant scene, e.g., InstantCamera, RenderLight, etc.
class InstantObject : public Object {
public:
    ~InstantObject() override = default;

    /// Get the instant scene that this object belongs to.
    [[nodiscard]] InstantScene *getInstantScene() const {
        KRD_ASSERT(instantScene, "InstantObject: InstantScene is not set");
        return instantScene;
    }

    /// Get the non-zero unique identifier of this object in the instant scene.
    [[nodiscard]] uint64_t getInstantSceneId() const {
        KRD_ASSERT(instantSceneId != 0, "InstantObject: Render id is not set");
        return instantSceneId;
    }

    /// Get the scene that the instant scene observes.
    [[nodiscard]] Scene *getScene() const;

public:
    /// Try to pull the object from the scene.
    virtual bool pull() { return false; }

protected:
    explicit InstantObject(InstantScene *instantScene);

private:
    ///
    InstantScene *instantScene;
    ///
    uint64_t const instantSceneId;
};
} // namespace krd
