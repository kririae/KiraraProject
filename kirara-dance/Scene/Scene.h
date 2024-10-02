#pragma once

#include <unordered_map>
#include <unordered_set>

#include "Core/Object.h"
#include "Scene/SceneObject.h"

namespace krd {
class TriangleMesh;

class Scene final : public Object {
private:
    Scene() = default;

public:
    ///
    static Ref<Scene> create() { return {new Scene()}; }

    ///
    ~Scene() = default;

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
    /// \throw kira::Anyhow if the object cannot be casted to \c T.
    template <typename T>
        requires(std::is_base_of_v<SceneObjectBase, T>)
    [[nodiscard]] auto getAs(uint64_t sceneObjId) const {
        auto ret = sceneObjMap.at(sceneObjId).dyn_cast<T>();
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
    uint64_t registerSceneObject(Ref<SceneObjectBase> sceneObj);

    ///
    void markRenderable(uint64_t sceneObjId) {}

    ///
    void markPhysical(uint64_t sceneObjId) {}

    ///
    void markStaticTriangleMesh(uint64_t sceneObjId);

public:
    /// The iterable range of the scene objects of type \c T.
    template <typename T, typename IterableType> class SceneObjectRange {
    public:
        SceneObjectRange(Scene const *s, IterableType &iterable) : scene(s), iterable(iterable) {}

        class Iterator {
        private:
            Scene const *scene;
            typename IterableType::iterator it;

        public:
            Iterator(Scene const *s, typename IterableType::iterator i) : scene(s), it(i) {}

            bool operator!=(Iterator const &other) const { return it != other.it; }
            Iterator &operator++() {
                ++it;
                return *this;
            }
            auto operator*() const { return scene->template getAs<T>(*it); }
        };

        Iterator begin() { return Iterator(scene, iterable.begin()); }
        Iterator end() { return Iterator(scene, iterable.end()); }

    private:
        Scene const *scene;
        IterableType &iterable;
    };

    ///
    [[nodiscard]] auto getStaticTriangleMeshRange() const {
        return SceneObjectRange<TriangleMesh, std::unordered_set<uint64_t> const>{
            this, staticTriangleMeshSceneObjIds
        };
    }

private:
    ///
    std::atomic_uint64_t sceneObjIdCnt{0};
    ///
    std::unordered_map<uint64_t, Ref<SceneObjectBase>> sceneObjMap;
    ///
    std::unordered_set<uint64_t> renderableSceneObjIds;
    ///
    std::unordered_set<uint64_t> physicalSceneObjIds;
    ///
    std::unordered_set<uint64_t> staticTriangleMeshSceneObjIds;
};
} // namespace krd
