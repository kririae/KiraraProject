#pragma once

#include "Renderer/RenderObject.h"
#include "Scene/Scene.h"

namespace krd {
class RenderScene final : public Object {
private:
    /// Initiate the \c RenderScene with the given scene in observer pattern.
    RenderScene(Ref<Scene> scene);

public:
    /// \see RenderScene::RenderScene
    [[nodiscard]]
    static Ref<RenderScene> create(Ref<Scene> scene) {
        return {new RenderScene(std::move(scene))};
    }

    /// Creates a render object and registers it to the \c RenderScene.
    template <typename T, typename... Args>
        requires(std::is_base_of_v<RenderObject, T>)
    auto create(Args &&...args) {
        return T::create(this, std::forward<Args>(args)...);
    }

public:
    /// Registers a render object to the render scene.
    ///
    /// Invoked mostly by \c RenderObject constructor.
    ///
    /// \return A non-zero unique identifier of the render object.
    uint64_t registerRenderObject(Ref<RenderObject> renderObj);

private:
    /// Get the reference scene that this \c RenderScene observes.
    auto getScene() const { return scene; }

    void doInit(Ref<Scene> const &scene); // (all)
    void initStaticTriangleMeshes();      // (1)

private:
    ///
    Ref<Scene> scene;
    ///
    std::atomic_uint64_t renderObjIdCnt{0};
    ///
    std::unordered_map<uint64_t, Ref<RenderObject>> renderObjMap;
};
} // namespace krd
