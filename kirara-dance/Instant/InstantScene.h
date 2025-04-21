#pragma once

#include "Core/ProgramBuilder.h"
#include "Instant/InstantObject.h"
#include "Scene/Scene.h"

namespace krd {
class InstantCamera;
class SlangGraphicsContext;

class InstantScene final : public Object {
public:
    struct Desc {
        ///
    };

    /// The empty base class for the device data of the \c InstantScene.
    ///
    /// \c DeviceData should be self-contained, i.e., should not have any raw reference to the
    /// context.
    ///
    /// \c DeviceData don't have to be a POD-like type.
    struct DeviceData : Object {};

private:
    /// Initiate the \c InstantScene with the given scene in observer pattern.
    InstantScene(Desc const &desc, Ref<Scene> scene);

    /// Destructor.
    ~InstantScene() override;

public:
    /// \see InstantScene::InstantScene
    [[nodiscard]] static Ref<InstantScene> create(Desc const &desc, Ref<Scene> scene) {
        return {new InstantScene(desc, std::move(scene))};
    }

    /// Creates a render object and registers it to the \c InstantScene.
    template <typename T, typename... Args>
        requires(std::is_base_of_v<InstantObject, T>)
    auto create(Args &&...args) {
        return T::create(this, std::forward<Args>(args)...);
    }

    /// Get the render object of type \c T with the given id.
    ///
    /// \tparam T The expected type of the render object.
    ///
    /// \throw std::out_of_range if the id is out of range.
    /// \throw kira::Anyhow if the object cannot be cast to \c T.
    template <typename T>
        requires(std::is_base_of_v<InstantObject, T>)
    [[nodiscard]] auto getAs(uint64_t iObjId) const {
        auto ret = iObjMap.at(iObjId).template dyn_cast<T>();
        if (!ret)
            throw kira::Anyhow("InstantScene: Render object cannot be casted to the given type");
        return ret;
    }

public:
    /// Registers a render object to the instant scene.
    ///
    /// Invoked mostly by \c InstantObject constructor.
    ///
    /// \return A non-zero unique identifier of the render object.
    [[nodiscard]] uint64_t registerInstantObject(Ref<InstantObject> rObj) noexcept;

    /// Get the reference scene that this \c InstantScene observes.
    [[nodiscard]] auto getScene() const { return scene; }

    ///
    [[nodiscard]] Ref<InstantCamera> getActiveCamera() const;

    /// Pull the render objects from the scene.
    void pull();

public:
    /// Get the render object of type \c T with the given id.
    ///
    /// \remark This function is not thread-safe and highly inefficient. Use it only for development
    /// and debugging.
    template <typename T> [[nodiscard]] auto getInstantObjectOfType() const {
        kira::SmallVector<Ref<T>> ret;
        for (auto const &[_, iObj] : iObjMap)
            if (auto casted = iObj.template dyn_cast<T>(); casted)
                ret.push_back(casted);
        return ret;
    }

public:
    // Slang-related functions
    //

    /// Create the program builder that describes the rendering pipeline required for the scene.
    [[nodiscard]] ProgramBuilder createProgramBuilder() const;

private:
    void doInit(Ref<Scene> const &scene); // (all)
    void initTriangleMeshes();            // (1)
    void initCameras();                   // (2)

private:
    // Destruction order matters here. sObj will potentially be managed by iObj, so iObj destructs
    // first.
    uint64_t activeCameraId{0};

    ///
    Ref<Scene> scene;
    ///
    std::atomic_uint64_t iObjIdCnt{0};
    ///
    std::unordered_map<uint64_t, Ref<InstantObject>> iObjMap;
};
} // namespace krd
