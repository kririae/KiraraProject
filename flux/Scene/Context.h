#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "flux/Scene/ContextIndexMap.h"
#include "flux/Scene/ImageAsset.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"
#include "kira/FileResolver.h"
#include "kira/SmallVector.h"

namespace flux {
class PathIntegrator;
class Sampler;
class BSDF;
class EDF;
class ImageTexture;

/// \brief Owns the host-side objects in a Flux scene.
///
/// Context mutation is single-threaded. Callers must serialize \c create and
/// \c commit.
class Context final : public Object {
    friend class TXContext;

public:
    /// \brief Creates an empty context.
    [[nodiscard]] static Ref<Context> create();

    /// \brief Releases the context's ownership of its objects.
    ~Context() override;

    /// \brief Creates \c T and atomically absorbs its construction transaction.
    ///
    /// Nested calls through the supplied \c TXContext join the same
    /// transaction. If construction or registration throws, the context keeps
    /// none of the transaction's objects.
    template <IsConfigurableObject T>
    [[nodiscard]] Ref<T> create(kira::Properties const &props = {}) {
        TXContext tx(*this);
        auto object = tx.create<T>(props);
        absorb(std::move(tx));
        return object;
    }

    /// \brief Gets object \p contextId as \c T.
    ///
    /// \throw std::out_of_range if the ID is unknown.
    /// \throw kira::Anyhow if the object is not a \c T.
    template <IsContextObject T> [[nodiscard]] Ref<T> get(std::size_t contextId) {
        auto const iterator = objects_.find(contextId);
        if (iterator == objects_.end())
            throw std::out_of_range("Context: object ID is out of range");

        auto object = iterator->second.template dynamicCast<T>();
        if (!object)
            throw kira::Anyhow("Context: object has the wrong type");
        return object;
    }

    /// \brief Gets object \p contextId as a const \c T.
    ///
    /// \throw std::out_of_range if the ID is unknown.
    /// \throw kira::Anyhow if the object is not a \c T.
    template <IsContextObject T> [[nodiscard]] Ref<T const> get(std::size_t contextId) const {
        auto const iterator = objects_.find(contextId);
        if (iterator == objects_.end())
            throw std::out_of_range("Context: object ID is out of range");

        auto object = iterator->second.template dynamicCast<T const>();
        if (!object)
            throw kira::Anyhow("Context: object has the wrong type");
        return object;
    }

    /// \brief Returns the number of objects owned by this context.
    [[nodiscard]] std::size_t getNumContextObjects() const noexcept { return objects_.size(); }

    /// \brief Returns the resolver used by later object construction.
    [[nodiscard]] kira::FileResolver &getFileResolver() noexcept { return fileResolver_; }
    [[nodiscard]] kira::FileResolver const &getFileResolver() const noexcept {
        return fileResolver_;
    }

    [[nodiscard]] ImageAssetPool &getImageAssetPool() noexcept { return imageAssetPool_; }

    [[nodiscard]] ContextIndexMap::Index getImageTextureIndex(std::size_t contextId) const {
        return imageTextures_.getIndex(contextId);
    }
    [[nodiscard]] ContextIndexMap::Index getBSDFIndex(std::size_t contextId) const {
        return bsdfs_.getIndex(contextId);
    }
    [[nodiscard]] ContextIndexMap::Index getEDFIndex(std::size_t contextId) const {
        return edfs_.getIndex(contextId);
    }

    [[nodiscard]] ContextIndexMap::Index getImageTextureIndexLimit() const noexcept {
        return imageTextures_.getIndexLimit();
    }
    [[nodiscard]] ContextIndexMap::Index getBSDFIndexLimit() const noexcept {
        return bsdfs_.getIndexLimit();
    }
    [[nodiscard]] ContextIndexMap::Index getEDFIndexLimit() const noexcept {
        return edfs_.getIndexLimit();
    }

    /// \brief Returns the first integrator successfully added to this context.
    ///
    /// \throw kira::Anyhow If the context has no integrator.
    [[nodiscard]] Ref<PathIntegrator const> getActiveIntegrator() const;

    /// \brief Returns the first sampler successfully added to this context.
    ///
    /// \throw kira::Anyhow If the context has no sampler.
    [[nodiscard]] Ref<Sampler const> getActiveSampler() const;

    /// \brief Returns every context object that is a \c T or derives from it.
    ///
    /// Results are ordered by context ID.
    template <IsContextObject T> [[nodiscard]] kira::SmallVector<Ref<T const>> getObjects() const {
        if constexpr (std::same_as<T, ImageTexture>)
            return getIndexedObjects<T>(imageTextures_);
        if constexpr (std::same_as<T, BSDF>)
            return getIndexedObjects<T>(bsdfs_);
        if constexpr (std::same_as<T, EDF>)
            return getIndexedObjects<T>(edfs_);

        kira::SmallVector<Ref<T const>> result;
        for (auto const &entry : objects_)
            if (auto typed = entry.second.template dynamicCast<T const>())
                result.push_back(std::move(typed));

        std::ranges::sort(result, {}, [](auto const &object) { return object->getContextId(); });
        return result;
    }

    /// \brief Commits changes owned by this context.
    void commit() noexcept;

private:
    Context() = default;

    [[nodiscard]] std::size_t allocateId() noexcept { return nextId_++; }
    void absorb(TXContext &&tx);

    template <IsContextObject T>
    [[nodiscard]] kira::SmallVector<Ref<T const>>
    getIndexedObjects(ContextIndexMap const &indices) const {
        kira::SmallVector<Ref<T const>> result;
        result.reserve(indices.size());
        for (auto const &[contextId, unused] : indices.entries_)
            result.push_back(get<T>(contextId));
        return result;
    }

    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    ContextIndexMap imageTextures_;
    ContextIndexMap bsdfs_;
    ContextIndexMap edfs_;
    std::optional<std::size_t> activeIntegratorId_;
    std::optional<std::size_t> activeSamplerId_;
    ImageAssetPool imageAssetPool_;
    kira::FileResolver fileResolver_;
    std::size_t nextId_{0};
};
} // namespace flux
