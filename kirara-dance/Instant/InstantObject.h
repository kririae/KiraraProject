#pragma once

#include "Core/Object.h"

namespace krd {
class InstantScene;
class Scene;

/// Base class for an object that belongs to a instant scene, e.g., \c InstantCamera, etc.
class InstantObject : public Object {
public:
    ~InstantObject() noexcept override = default;

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
    /// Construct a instant scene object and registers it to the instant scene.
    explicit InstantObject(InstantScene *instantScene) noexcept;

private:
    ///
    InstantScene *const instantScene;
    ///
    uint64_t const instantSceneId;
};
} // namespace krd
