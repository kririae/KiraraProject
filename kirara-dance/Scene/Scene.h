#pragma once

#include <kira/SmallVector.h>

#include <unordered_map>
#include <unordered_set>

#include "Core/Object.h"
#include "Scene/SceneObject.h"

namespace krd {
class Camera;
class TriangleMesh;

class Scene final : public Object {
    Scene() = default;

public:
    friend class SceneController;

    struct Desc {
        ///
    };

    ///
    static Ref<Scene> create(Desc const &desc) { return {new Scene()}; }

    ///
    ~Scene() override;

    /// Creates a scene object and registers it to the \c Scene.
    template <typename T, typename... Args>
        requires(std::is_base_of_v<SceneObjectBase, T>)
    auto create(Args &&...args) {
        return T::create(this, std::forward<Args>(args)...);
    }

    /// Get the scene object of type \c T with the given id.
    ///
    /// \tparam T The expected type of the scene object.
    ///
    /// \throw std::out_of_range if the id is out of range.
    /// \throw kira::Anyhow if the object cannot be cast to \c T.
    template <typename T>
        requires(std::is_base_of_v<SceneObjectBase, T>)
    [[nodiscard]] auto getAs(uint64_t sceneObjId) const {
        auto ret = sObjMap.at(sceneObjId).dyn_cast<T>();
        if (!ret)
            throw kira::Anyhow("Scene: Scene object cannot be casted to the given type");
        return ret;
    }

public:
    /// Registers a scene object to the scene.
    ///
    /// Invoked mostly by \c SceneObject constructor.
    ///
    /// \return A non-zero unique identifier of the scene object.
    [[nodiscard]] uint64_t registerSceneObject(Ref<SceneObjectBase> sObj);

    /// Discard a scene object from the scene.
    bool discardSceneObject(uint64_t sObjId);

    ///
    void markRenderable(uint64_t sObjId) {}

    ///
    void markPhysical(uint64_t sObjId) {}

    ///
    void markTriangleMesh(uint64_t sObjId) {}

    /// Mark the scene object as the active camera.
    void markActiveCamera(uint64_t const sObjId) { activeCameraId = sObjId; }

    /// Unmark the scene object as the active camera.
    void unmarkActiveCamera(uint64_t const sObjId) {
        if (sObjId == activeCameraId)
            activeCameraId = 0;
    }

    /// Get the scene object of type \c T with the given id.
    ///
    /// \remark This function is not thread-safe and highly inefficient. Use it only for development
    /// and debugging.
    template <typename T> [[nodiscard]] auto getSceneObjectOfType() const {
        kira::SmallVector<Ref<T>> ret;
        for (auto const &[_, sObj] : sObjMap)
            if (auto casted = sObj.dyn_cast<T>(); casted)
                ret.push_back(casted);
        return ret;
    }

    /// Get the active camera in the scene.
    [[nodiscard]] std::optional<Ref<Camera>> getActiveCamera() const;

    /// Get the triangle meshes in the scene.
    [[nodiscard]] kira::SmallVector<Ref<TriangleMesh>> getTriangleMesh() const;

private:
    ///
    uint64_t activeCameraId{0};
    ///
    std::atomic_uint64_t sObjIdCnt{0};
    ///
    std::unordered_map<uint64_t, Ref<SceneObjectBase>> sObjMap;
};
} // namespace krd
