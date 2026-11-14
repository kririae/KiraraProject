#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <unordered_map>
#include <utility>

#include "flux/Core/Object.h"
#include "flux/Scene/ContextIndexMap.h"
#include "kira/Anyhow.h"

namespace kira {
class FileResolver;
}

namespace flux {
class Context;
class ImageAssetPool;
class ShaderAssetPool;

/// \brief Holds the objects that a caller creates before they are added to the scene.
class TXContext {
    friend class Context;
    friend class ContextObject;

public:
    TXContext(TXContext const &) = delete;
    TXContext &operator=(TXContext const &) = delete;

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
            registerObject(object);
            return object;
        }
    }

    /// \brief Gets an object from this transaction or its owning context.
    ///
    /// The object is const, so a constructor binds it but does not edit it.
    template <IsContextObject T> [[nodiscard]] Ref<T const> get(std::size_t contextId) const {
        auto object = getObject(contextId).template dynamicCast<T const>();
        if (!object)
            throw kira::Anyhow("TXContext: object has the wrong type");
        return object;
    }

    /// \brief Returns whether \p object is an object of this transaction or of its owning context.
    [[nodiscard]] bool contains(ContextObject const &object) const;

    /// \brief Returns the resolver for the files a constructor loads.
    [[nodiscard]] kira::FileResolver const &getFileResolver() const noexcept;

    /// \brief Returns the pool that shares loaded images.
    [[nodiscard]] ImageAssetPool &getImageAssetPool() const noexcept;

    /// \brief Returns the pool that shares generated shaders.
    [[nodiscard]] ShaderAssetPool &getShaderAssetPool() const noexcept;

private:
    explicit TXContext(Context &context);

    [[nodiscard]] Ref<ContextObject> getObject(std::size_t contextId) const;
    [[nodiscard]] std::size_t allocateId();
    void registerObject(Ref<ContextObject> object);

    Context &context_;
    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;

    /// IDs of the objects of each indexed kind, which the context merges into its index maps.
    std::array<ContextIndexMap::Transaction, numIndexedKinds> indices_;
};
} // namespace flux
