#pragma once

#include "Core/Object.h"

namespace krd {
class Scene;

/// Base class for an object that belongs to a scene, e.g., Camera, Light, Geometry, etc.
class SceneObject : public Object {
public:
    ~SceneObject() noexcept override = default;

    /// Gets the scene that this object belongs to.
    [[nodiscard]] Scene *getScene() const {
        KRD_ASSERT(scene, "SceneObject: Scene is not set");
        return scene;
    }

    /// Gets the non-zero unique identifier of this object in the scene.
    [[nodiscard]] auto getSceneId() const {
        KRD_ASSERT(sceneId != 0, "SceneObject: Scene id is not set");
        return sceneId;
    }

public:
    /// \brief Initialize the scene object.
    ///
    /// This will be called after the object is constructed.
    ///
    /// Do the job that needs to be done after the object is constructed here, for example, load
    /// data from the disk.
    virtual void init() {}

    /// \brief Reset the animation status the scene object.
    virtual void resetAnimation() {}

    /// \brief Tick the scene object.
    ///
    /// This will be executed every frame at first.
    virtual void tick(float deltaTime) { (void)(deltaTime); }

protected:
    /// Constructs a scene object and registers it to the scene.
    ///
    /// \remark \c SceneObject constructor should never throw an exception.
    explicit SceneObject(Scene *scene) noexcept;

private:
    /// The scene that this object belongs to.
    Scene *const scene;
    /// The unique identifier of this object in the scene.
    uint64_t const sceneId;

#if 0
    /// Flag to indicate whether this object is dirty. Managed by the scene or simulator.
    std::atomic_bool isDirty{true};
    /// Flag to indicate whether this object is safe to be accessed.
    std::atomic_bool isSafe{true};
#endif
};
} // namespace krd
