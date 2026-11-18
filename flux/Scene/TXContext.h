#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <unordered_map>
#include <utility>

#include "flux/Core/Object.h"
#include "flux/Core/ResolvedPath.h"
#include "flux/Scene/ContextIndexMap.h"
#include "kira/Anyhow.h"

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
    template <IsContextObject T> [[nodiscard]] Ref<T const> get(std::size_t contextId) const {
        auto object = getObject(contextId).template dynamicCast<T const>();
        if (!object)
            throw kira::Anyhow("TXContext: object has the wrong type");
        return object;
    }

    /// \brief Returns whether \p object is an object of this transaction or of its owning context.
    [[nodiscard]] bool contains(ContextObject const &object) const;

    [[nodiscard]] ResolvedPath const &getAnchor() const noexcept { return anchor_; }

    [[nodiscard]] ImageAssetPool &getImageAssetPool() const noexcept;
    [[nodiscard]] ShaderAssetPool &getShaderAssetPool() const noexcept;

private:
    TXContext(Context &context, ResolvedPath anchor);

    [[nodiscard]] Ref<ContextObject> getObject(std::size_t contextId) const;
    [[nodiscard]] std::size_t allocateId();
    void registerObject(Ref<ContextObject> object);

    Context &context_;
    ResolvedPath anchor_;
    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;

    /// IDs of the objects of each indexed kind, which the context merges into its index maps.
    std::array<ContextIndexMap::Transaction, numIndexedKinds> indices_;
};
} // namespace flux
