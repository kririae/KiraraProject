#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>

#include "flux/Core/Object.h"
#include "flux/Scene/ContextIndexMap.h"
#include "kira/Anyhow.h"

namespace flux {
class PathIntegrator;
class Sampler;
class ImageTexture;
class EnvMapLight;
class TriangleMesh;

/// \brief Collects objects created by one \c Context::create call.
///
/// A transaction belongs to one context. The context absorbs it only after the
/// requested object has been fully constructed and registered.
class TXContext {
    friend class Context;
    friend class ContextObject;
    friend class PathIntegrator;
    friend class Sampler;
    friend class ImageTexture;
    friend class EnvMapLight;
    friend class TriangleMesh;

public:
    /// \brief Creates and registers a configurable object in this transaction.
    ///
    /// \p args reach a \c T::create that returns a \c Ref<T>, and otherwise a
    /// \c T constructor. Base context object types provide such a \c create to
    /// select a concrete type inside this transaction. Concrete types grant
    /// \c TXContext access to their constructors.
    template <IsConfigurableObject T, typename... Args>
    [[nodiscard]] Ref<T> create(Args &&...args) {
        if constexpr (requires {
                          {
                              T::create(std::declval<TXContext &>(), std::declval<Args>()...)
                          } -> std::same_as<Ref<T>>;
                      }) {
            return T::create(*this, std::forward<Args>(args)...);
        } else {
            Ref<T> object{new T(*this, std::forward<Args>(args)...)};
            static_cast<ContextObject *>(object.get())->registerTo(*this);
            return object;
        }
    }

    /// \brief Gets an object from this transaction or its owning context.
    template <IsContextObject T> [[nodiscard]] Ref<T> get(std::size_t contextId) const {
        auto object = getObject(contextId).template dynamicCast<T>();
        if (!object)
            throw kira::Anyhow("TXContext: object has the wrong type");
        return object;
    }

private:
    explicit TXContext(Context &context) noexcept;

    [[nodiscard]] Context &getContext() const noexcept { return context_; }
    [[nodiscard]] Ref<ContextObject> getObject(std::size_t contextId) const;
    [[nodiscard]] std::size_t allocateId();
    void registerObject(Ref<ContextObject> object);
    void stageActiveIntegrator(std::size_t contextId) noexcept;
    void stageActiveSampler(std::size_t contextId) noexcept;
    void stageActiveEnvMap(std::size_t contextId) noexcept;

    Context &context_;
    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    ContextIndexMap::Transaction imageTextures_;
    ContextIndexMap::Transaction bsdfs_;
    ContextIndexMap::Transaction edfs_;
    std::optional<std::size_t> activeIntegratorId_;
    std::optional<std::size_t> activeSamplerId_;
    std::optional<std::size_t> activeEnvMapId_;
};
} // namespace flux
