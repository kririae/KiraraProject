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

/// \brief Holds the objects of one \c Context::create call before they join the scene.
///
/// A transaction lives only inside that call; a constructor receives it by reference and does
/// not keep it. Its objects have IDs but no owner, so they appear in no scene query or record.
/// The context absorbs the whole transaction at once, or none of it.
class TXContext {
    friend class Context;
    friend class ContextObject;

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
            registerObject(object);
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

    /// \brief Returns the resolver for the files a constructor loads.
    [[nodiscard]] kira::FileResolver const &getFileResolver() const noexcept;

    /// \brief Returns the pool that shares loaded images.
    [[nodiscard]] ImageAssetPool &getImageAssetPool() const noexcept;

private:
    explicit TXContext(Context &context) noexcept;

    [[nodiscard]] Ref<ContextObject> getObject(std::size_t contextId) const;
    [[nodiscard]] std::size_t allocateId();
    void registerObject(Ref<ContextObject> object);

    Context &context_;
    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;

    /// IDs of the objects of each indexed kind, which the context merges into its index maps.
    std::array<ContextIndexMap::Transaction, numIndexedKinds> indices_;
};
} // namespace flux
