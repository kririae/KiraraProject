#pragma once

#include "Core/ProgramBuilder.h"
#include "Renderer/RenderObject.h"
#include "Scene/Scene.h"

namespace krd {
class SlangGraphicsContext;

class RenderScene final : public Object {
public:
    struct Desc {
        ///
    };

private:
    /// Initiate the \c RenderScene with the given scene in observer pattern.
    RenderScene(Desc const &desc, Ref<Scene> scene);

public:
    /// \see RenderScene::RenderScene
    [[nodiscard]] static Ref<RenderScene> create(Desc const &desc, Ref<Scene> scene) {
        return {new RenderScene(desc, std::move(scene))};
    }

    /// Creates a render object and registers it to the \c RenderScene.
    template <typename T, typename... Args>
        requires(std::is_base_of_v<RenderObject, T>)
    auto create(Args &&...args) {
        return T::create(this, std::forward<Args>(args)...);
    }

    /// Get the render object of type \c T with the given id.
    ///
    /// \tparam T The expected type of the render object.
    ///
    /// \throw std::out_of_range if the id is out of range.
    /// \throw kira::Anyhow if the object cannot be casted to \c T.
    template <typename T>
        requires(std::is_base_of_v<RenderObject, T>)
    [[nodiscard]] auto getAs(uint64_t sceneObjId) const {
        auto ret = rObjMap.at(sceneObjId).dyn_cast<T>();
        if (!ret)
            throw kira::Anyhow("RenderScene: Render object cannot be casted to the given type");
        return ret;
    }

public:
    /// Registers a render object to the render scene.
    ///
    /// Invoked mostly by \c RenderObject constructor.
    ///
    /// \return A non-zero unique identifier of the render object.
    [[nodiscard]] uint64_t registerRenderObject(Ref<RenderObject> rObj);

    /// Bind the graphics context to the render scene.
    void bindGraphicsContext(Ref<SlangGraphicsContext> ctx);

    /// Get the reference scene that this \c RenderScene observes.
    [[nodiscard]] auto getScene() const { return scene; }

    /// Get the graphics context that this \c RenderScene is associated with.
    [[nodiscard]] auto getGraphicsContext() const { return ctx; }

    /// Pull the render objects from the scene.
    void pull();

public:
    /// Get the render object of type \c T with the given id.
    ///
    /// \remark This function is not thread-safe and highly inefficient. Use it only for development
    /// and debugging.
    template <typename T> [[nodiscard]] auto getRenderObjectOfType() const {
        kira::SmallVector<Ref<T>> ret;
        for (auto const &[_, rObj] : rObjMap)
            if (auto casted = rObj.dyn_cast<T>(); casted)
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

private:
    ///
    Ref<Scene> scene;
    ///
    Ref<SlangGraphicsContext> ctx;
    ///
    std::atomic_uint64_t rObjIdCnt{0};
    ///
    std::unordered_map<uint64_t, Ref<RenderObject>> rObjMap;
};
} // namespace krd
