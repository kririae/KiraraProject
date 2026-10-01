#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "flux/Core/EnumFlags.h"
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
class EnvMapLight;

/// \brief Owns the host-side objects in a Flux scene.
///
/// Context mutation is single-threaded. Callers must serialize \c create,
/// \c commit, \c clearDirty, and the setters of its objects, which list changed
/// objects with the context.
class Context final : public Object {
    friend class TXContext;
    friend class ContextObject;

public:
    /// \brief Changes to the context itself, as opposed to changes to one object.
    ///
    /// An active-object setter records its bit only when the active object changes.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        Added = 1U << 0U,
        ActiveIntegrator = 1U << 1U,
        ActiveSampler = 1U << 2U,
        ActiveEnvMap = 1U << 3U,
    };

    /// \brief Creates an empty context.
    [[nodiscard]] static Ref<Context> create();

    /// \brief Releases the context's ownership of its objects.
    ~Context() override;

    /// \brief Creates \c T and atomically absorbs its construction transaction.
    ///
    /// \p args reach \c TXContext::create. Nested calls through the supplied
    /// \c TXContext join the same transaction. If construction or registration
    /// throws, the context keeps none of the transaction's objects.
    template <IsConfigurableObject T, typename... Args>
    [[nodiscard]] Ref<T> create(Args &&...args) {
        TXContext tx(*this);
        auto object = tx.create<T>(std::forward<Args>(args)...);
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

    /// \brief Returns the index assigned to \p contextId in the map of \c T's kind.
    ///
    /// \throw std::out_of_range If \p contextId is not in that map.
    template <IsIndexedObject T>
    [[nodiscard]] ContextIndexMap::Index getIndex(std::size_t contextId) const {
        return getMap<T>().getIndex(contextId);
    }

    /// \brief Returns one past the largest index ever assigned in the map of \c T's kind.
    template <IsIndexedObject T>
    [[nodiscard]] ContextIndexMap::Index getIndexLimit() const noexcept {
        return getMap<T>().getIndexLimit();
    }

    /// \brief Returns the active integrator, initially the first one added.
    ///
    /// \throw kira::Anyhow If the context has no integrator.
    [[nodiscard]] Ref<PathIntegrator const> getActiveIntegrator() const;

    /// \brief Makes \p integrator the active integrator.
    ///
    /// \throw kira::Anyhow If \p integrator is null or belongs to another context.
    void setActiveIntegrator(Ref<PathIntegrator const> integrator);

    /// \brief Returns the active sampler, initially the first one added.
    ///
    /// \throw kira::Anyhow If the context has no sampler.
    [[nodiscard]] Ref<Sampler const> getActiveSampler() const;

    /// \brief Makes \p sampler the active sampler.
    ///
    /// \throw kira::Anyhow If \p sampler is null or belongs to another context.
    void setActiveSampler(Ref<Sampler const> sampler);

    /// \brief Returns the active environment map, initially the first one added.
    ///
    /// Returns an empty reference when the context has no environment map.
    [[nodiscard]] Ref<EnvMapLight const> getActiveEnvMap() const;

    /// \brief Makes \p envMap the active environment map.
    ///
    /// An empty reference leaves the context without an environment map.
    ///
    /// \throw kira::Anyhow If \p envMap belongs to another context.
    void setActiveEnvMap(Ref<EnvMapLight const> envMap);

    /// \brief Returns every context object that is a \c T or derives from it.
    ///
    /// Results are ordered by context ID.
    template <IsContextObject T> [[nodiscard]] kira::SmallVector<Ref<T const>> getObjects() const {
        if constexpr (IsIndexedObject<T>)
            return getIndexedObjects<T>(getMap<T>());

        kira::SmallVector<Ref<T const>> result;
        for (auto const &entry : objects_)
            if (auto typed = entry.second.template dynamicCast<T const>())
                result.push_back(std::move(typed));

        std::ranges::sort(result, {}, [](auto const &object) { return object->getContextId(); });
        return result;
    }

    /// \brief Commits changes owned by this context.
    void commit() noexcept;

    /// \brief Returns the context changes since the last \c clearDirty.
    [[nodiscard]] DirtyBits getDirtyBits() const noexcept { return dirtyBits_; }

    /// \brief Returns the IDs of objects added since the last \c clearDirty.
    ///
    /// The IDs are ascending.
    [[nodiscard]] std::span<std::size_t const> getAddedIds() const noexcept { return addedIds_; }

    /// \brief Returns the IDs of objects changed since the last \c clearDirty.
    ///
    /// Each ID is listed once, in the order its object first changed.
    [[nodiscard]] std::span<std::size_t const> getChangedIds() const noexcept {
        return changedIds_;
    }

    /// \brief Returns the number of times \c clearDirty was called.
    [[nodiscard]] std::uint64_t getEpoch() const noexcept { return epoch_; }

    /// \brief Zeroes the context bits and the bits of every changed object, empties the added and
    ///        changed lists, and starts the next epoch.
    void clearDirty() noexcept;

private:
    Context() = default;

    [[nodiscard]] std::size_t allocateId() noexcept { return nextId_++; }
    void absorb(TXContext &&tx);

    /// \brief Replaces \p slot with \p id and records \p bits when the active object changes.
    void setActive(std::optional<std::size_t> &slot, std::optional<std::size_t> id, DirtyBits bits);

    template <IsIndexedObject T> [[nodiscard]] ContextIndexMap const &getMap() const noexcept {
        return indices_[static_cast<std::size_t>(T::indexedKind)];
    }

    /// \brief Returns the objects of \p indices that are a \c T, ordered by ID.
    ///
    /// A \c T below the kind's base class selects only part of the map.
    template <IsContextObject T>
    [[nodiscard]] kira::SmallVector<Ref<T const>>
    getIndexedObjects(ContextIndexMap const &indices) const {
        kira::SmallVector<Ref<T const>> result;
        result.reserve(indices.size());
        for (auto const &[contextId, unused] : indices.entries_)
            if (auto typed = objects_.at(contextId).template dynamicCast<T const>())
                result.push_back(std::move(typed));
        return result;
    }

    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    std::array<ContextIndexMap, numIndexedKinds> indices_;
    std::optional<std::size_t> activeIntegratorId_;
    std::optional<std::size_t> activeSamplerId_;
    std::optional<std::size_t> activeEnvMapId_;
    ImageAssetPool imageAssetPool_;
    kira::FileResolver fileResolver_;
    std::size_t nextId_{0};

    /// IDs of objects changed this epoch. An object is listed when its first bit is recorded, so
    /// it is listed once. Every listed ID is in \c objects_.
    std::vector<std::size_t> changedIds_;

    /// IDs of objects added this epoch, ascending. An added object is born clean, so it is listed
    /// here and not in \c changedIds_.
    std::vector<std::size_t> addedIds_;
    DirtyBits dirtyBits_{DirtyBits::None};
    std::uint64_t epoch_{0};
};

template <> inline constexpr bool isEnumFlags<Context::DirtyBits> = true;
} // namespace flux
