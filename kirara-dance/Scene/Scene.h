#pragma once

#include <kira/SmallVector.h>

#include <unordered_map>
#include <unordered_set>

#include "Core/Object.h"
#include "Scene/SceneGraph.h"
#include "Scene/SceneObject.h"

namespace krd {
class Camera;
class TriangleMesh;

class Scene final : public Object {
    Scene() = default;

public:
    struct Desc {
        ///
    };

    ///
    static Ref<Scene> create(Desc const &desc) { return {new Scene()}; }

    ///
    ~Scene() override;

    /// Creates a scene object and registers it to the \c Scene.
    template <typename T, typename... Args>
        requires(std::is_base_of_v<SceneObject, T>)
    auto create(Args &&...args) {
        return T::create(this, std::forward<Args>(args)...);
    }

    /// Get the scene object of type \c T with the given id.
    ///
    /// \tparam T The expected type of the scene object.
    ///
    /// \throw std::out_of_range if the id is out of range.
    /// \throw kira::Anyhow if the object can't be cast to \c T.
    template <typename T>
        requires(std::is_base_of_v<SceneObject, T>)
    [[nodiscard]] auto getAs(uint64_t sObjId) const {
        auto ret = sObjMap.at(sObjId).template dyn_cast<T>();
        if (!ret)
            throw kira::Anyhow("Scene: Scene object cannot be casted to the given type");
        return ret;
    }

    /// Tick the scene.
    void tick(float deltaTime);

public:
    /// Registers a scene object to the scene.
    ///
    /// Invoked mostly by \c SceneObject constructor.
    ///
    /// \return A non-zero unique identifier of the scene object.
    [[nodiscard]] uint64_t registerSceneObject(Ref<SceneObject> sObj) noexcept;

    /// Discard a scene object from the scene.
    ///
    /// \remark This function potentially invokes destructors.
    bool discardSceneObject(uint64_t sObjId) noexcept;

    /// Mark the scene object as the active camera.
    void markActiveCamera(uint64_t const sObjId) { activeCameraId = sObjId; }

    /// Unmark the scene object as the active camera.
    void unmarkActiveCamera(uint64_t const sObjId) {
        if (sObjId == activeCameraId)
            activeCameraId = 0;
    }

    /// Get the scene object of type \c T with the given id.
    ///
    /// \remark This function isn't thread-safe and highly inefficient. Use it only for development
    /// and debugging.
    template <typename T> [[nodiscard]] auto getSceneObjectOfType() const {
        kira::SmallVector<Ref<T>> ret;
        for (auto const &[_, sObj] : sObjMap)
            if (auto casted = sObj.template dyn_cast<T>(); casted)
                ret.push_back(casted);
        return ret;
    }

    /// Get the active camera in the scene.
    [[nodiscard]] std::optional<Ref<Camera>> getActiveCamera() const;

    /// Get the triangle meshes in the scene.
    [[nodiscard]] kira::SmallVector<Ref<TriangleMesh>> getTriangleMesh() const;

    [[nodiscard]] SceneGraph &getSceneGraph() { return sceneGraph; }
    [[nodiscard]] SceneGraph const &getSceneGraph() const { return sceneGraph; }

private:
    ///
    uint64_t activeCameraId{0};
    ///
    std::atomic_uint64_t sObjIdCnt{0};

    ///
    SceneGraph sceneGraph;

    /// The list of scene objects in the scene.
    ///
    /// Don't put anything after this line, the order of destruction is important.
    std::unordered_map<uint64_t, Ref<SceneObject>> sObjMap;
};
} // namespace krd
