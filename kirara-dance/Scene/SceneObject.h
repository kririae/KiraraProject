#pragma once

#include "Core/Object.h"

namespace krd {
class Scene;

// NOTE(krr): Renderable or not/Physical or not is a compile-time decision, which is quite different
// from the conventional ECS approach. This is more suitable for our observer pattern, where \c
// RenderScene and \c PhysicalScene will pull the objects that are renderable and physical,
// respectively in initialization stage.

/// Mark this object as renderable, i.e., should be initialized to the \c RenderScene.
template <typename Derived> class IRenderable {
public:
    /// Mark this object as renderable.
    IRenderable() { static_cast<Derived const &>(*this).markRenderable(); }
};

/// Mark this object as physical, i.e., should be initialized to the \c PhysicalScene.
template <typename Derived> class IPhysical {
public:
    /// Mark this object as physical.
    IPhysical() { static_cast<Derived const &>(*this).markPhysical(); }
};

/// Base class for an object that belongs to a scene, e.g., Camera, Light, Geometry, etc.
class SceneObjectBase : public Object {
public:
    template <typename Derived> friend class IRenderable;
    template <typename Derived> friend class IPhysical;

    ~SceneObjectBase() override = default;

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

protected:
    /// Constructs a scene object and registers it to the scene.
    SceneObjectBase(Scene *scene);

private:
    void markRenderable() const;
    void markPhysical() const;

    /// The scene that this object belongs to.
    Scene *scene{nullptr};

    /// The unique identifier of this object in the scene.
    uint64_t const sceneId{0};
};

/// A scene object that satisfies the given concepts.
///
/// Example:
/// \code{.cpp}
/// class StaticTriangleMesh : public SceneObject<> {};
/// class Camera : public SceneObject<IRenderable> {};
/// class DynamicTriangleMesh : public SceneObject<IRenderable, IPhysical> {};
/// \endcode
template <template <typename> class... Concept>
class SceneObject : public SceneObjectBase, public Concept<SceneObject<Concept...>>... {
public:
    ///
    explicit SceneObject(Scene *scene) : SceneObjectBase(scene) {}

    ///
    ~SceneObject() override = default;

public:
    /// Check if the scene object is renderable.
    ///
    /// \note This is a compile-time check.
    static constexpr bool isRenderable() {
        return std::is_base_of_v<IRenderable<SceneObject>, SceneObject>;
    }

    /// Check if the scene object is physical.
    ///
    /// \note This is a compile-time check.
    static constexpr bool isPhysical() {
        return std::is_base_of_v<IPhysical<SceneObject>, SceneObject>;
    }
};
} // namespace krd
