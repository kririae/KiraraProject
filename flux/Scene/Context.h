#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
/// A root is a type that no other object references, such as \c Primitive. The host removes a
/// root with \c remove. A dependent, such as \c BSDF, leaves when \c collectGarbage finds that
/// only the context references it. A removed object keeps its index until \c clearDirty.
///
/// An epoch is the interval between two \c clearDirty calls. The context records what changed in
/// the epoch for the runtimes that sync from it.
///
/// Edits may run concurrently: \c create, \c get, \c remove, the \c setActive functions, and
/// the setters of the objects. \c collectGarbage, \c clearDirty, and every query and runtime
/// sync run at a quiet point, with no edit in progress.
class Context final : public Object {
    friend class TXContext;
    friend class ContextObject;

public:
    /// \brief Changes to the context itself, as opposed to changes to one object.
    ///
    /// An active-object setter records its bit only when the active object changes.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        ActiveIntegrator = 1U << 0U,
        ActiveSampler = 1U << 1U,
        ActiveEnvMap = 1U << 2U,
    };

public:
    // Construction and destruction.

    /// \brief Creates an empty context.
    [[nodiscard]] static Ref<Context> create();

    /// \brief Releases the context's ownership of its objects.
    ~Context() override;

public:
    // Edits, callable at any time.

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
        std::scoped_lock const lock(mutex_);
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
        std::scoped_lock const lock(mutex_);
        auto const iterator = objects_.find(contextId);
        if (iterator == objects_.end())
            throw std::out_of_range("Context: object ID is out of range");

        auto object = iterator->second.template dynamicCast<T const>();
        if (!object)
            throw kira::Anyhow("Context: object has the wrong type");
        return object;
    }

    /// \brief Removes root object \p contextId from the scene at once.
    ///
    /// The object keeps living while the host holds a reference, but it has no owner. If it is
    /// the active integrator, sampler, or environment map, that slot becomes empty. An exception
    /// leaves the context unchanged.
    ///
    /// \throw std::out_of_range if the ID is unknown.
    /// \throw kira::Anyhow if the object is a dependent.
    void remove(std::size_t contextId);

    /// \brief Makes \p integrator the active integrator, or none if it is empty.
    ///
    /// \throw kira::Anyhow If \p integrator belongs to another context.
    void setActiveIntegrator(Ref<PathIntegrator const> integrator);

    /// \brief Makes \p sampler the active sampler, or none if it is empty.
    ///
    /// \throw kira::Anyhow If \p sampler belongs to another context.
    void setActiveSampler(Ref<Sampler const> sampler);

    /// \brief Makes \p envMap the active environment map, or none if it is empty.
    ///
    /// \throw kira::Anyhow If \p envMap belongs to another context.
    void setActiveEnvMap(Ref<EnvMapLight const> envMap);

public:
    // Quiet point.

    /// \brief Removes every dependent that only this context references.
    ///
    /// Repeats until a round removes nothing, because removing an object releases its
    /// references to others. Every round scans every dependent, which is deliberate: tracking
    /// which dependents lost a reference would put a record into every \c Ref release.
    void collectGarbage();

public:
    // Sync-time reads.

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

    /// \brief Returns the number of objects owned by this context.
    [[nodiscard]] std::size_t getNumContextObjects() const noexcept { return objects_.size(); }

    /// \brief Returns the index assigned to \p contextId in the map of \c T's kind.
    ///
    /// A removed object keeps its index until \c clearDirty.
    ///
    /// \throw std::out_of_range If \p contextId is not in that map.
    template <IsIndexedObject T>
    [[nodiscard]] ContextIndexMap::Index getIndex(std::size_t contextId) const {
        return getMap<T>().getIndex(contextId);
    }

    /// \brief Returns the index assigned to \p contextId in the map of \c T's kind, or nothing.
    template <IsIndexedObject T>
    [[nodiscard]] std::optional<ContextIndexMap::Index> findIndex(std::size_t contextId) const {
        auto const &entries = getMap<T>().entries_;
        auto const iterator = entries.find(contextId);
        if (iterator == entries.end())
            return std::nullopt;
        return iterator->second;
    }

    /// \brief Returns one past the largest index ever assigned in the map of \c T's kind.
    template <IsIndexedObject T>
    [[nodiscard]] ContextIndexMap::Index getIndexLimit() const noexcept {
        return getMap<T>().getIndexLimit();
    }

    /// \brief Returns the active integrator, or an empty reference.
    [[nodiscard]] Ref<PathIntegrator const> getActiveIntegrator() const;

    /// \brief Returns the active sampler, or an empty reference.
    [[nodiscard]] Ref<Sampler const> getActiveSampler() const;

    /// \brief Returns the active environment map, or an empty reference.
    [[nodiscard]] Ref<EnvMapLight const> getActiveEnvMap() const;

    /// \brief Returns the context changes since the last \c clearDirty.
    [[nodiscard]] DirtyBits getDirtyBits() const noexcept { return dirtyBits_; }

    /// \brief Returns the IDs of objects added since the last \c clearDirty.
    [[nodiscard]] std::unordered_set<std::size_t> const &getAddedIds() const noexcept {
        return addedIds_;
    }

    /// \brief Returns the IDs of objects changed since the last \c clearDirty.
    [[nodiscard]] std::unordered_set<std::size_t> const &getChangedIds() const noexcept {
        return changedIds_;
    }

    /// \brief Returns the IDs of objects removed since the last \c clearDirty.
    [[nodiscard]] std::unordered_set<std::size_t> const &getRemovedIds() const noexcept {
        return removedIds_;
    }

    /// \brief Returns the number of times \c clearDirty was called.
    [[nodiscard]] std::uint64_t getEpoch() const noexcept { return epoch_; }

public:
    // Epoch boundary.

    /// \brief Zeroes the bits of every changed object, erases the index of every removed object,
    ///        empties the three sets, clears the context bits, and starts the next epoch.
    ///
    /// An exception leaves the records in place, so a later call can finish the clear.
    void clearDirty();

public:
    // Resources.

    /// \brief Returns the resolver used by later object construction.
    [[nodiscard]] kira::FileResolver &getFileResolver() noexcept { return fileResolver_; }
    [[nodiscard]] kira::FileResolver const &getFileResolver() const noexcept {
        return fileResolver_;
    }

    [[nodiscard]] ImageAssetPool &getImageAssetPool() noexcept { return imageAssetPool_; }

private:
    Context() = default;

    [[nodiscard]] std::size_t allocateId() noexcept {
        return nextId_.fetch_add(1, std::memory_order_relaxed);
    }
    void absorb(TXContext &&tx);

    /// \brief Removes object \p contextId from the scene and records it as removed.
    ///
    /// An exception leaves the context unchanged.
    ///
    /// \pre The object is in \c objects_.
    void reclaim(std::size_t contextId);

    template <IsIndexedObject T> [[nodiscard]] ContextIndexMap const &getMap() const noexcept {
        return indices_[static_cast<std::size_t>(T::indexedKind)];
    }

    /// \brief Returns the objects of \p indices that are a \c T, ordered by ID.
    ///
    /// A \c T below the kind's base class selects only part of the map. A removed object
    /// keeps its entry until \c clearDirty and is skipped.
    template <IsContextObject T>
    [[nodiscard]] kira::SmallVector<Ref<T const>>
    getIndexedObjects(ContextIndexMap const &indices) const {
        kira::SmallVector<Ref<T const>> result;
        result.reserve(indices.size());
        for (auto const &[contextId, unused] : indices.entries_)
            if (auto const iterator = objects_.find(contextId); iterator != objects_.end())
                if (auto typed = iterator->second.template dynamicCast<T const>())
                    result.push_back(std::move(typed));
        return result;
    }

    /// Guards the edits that may run concurrently: absorbing a transaction, \c remove, the
    /// \c setActive functions, \c get, and the first change of an object.
    mutable std::mutex mutex_;

    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    std::array<ContextIndexMap, numIndexedKinds> indices_;
    Ref<PathIntegrator const> activeIntegrator_;
    Ref<Sampler const> activeSampler_;
    Ref<EnvMapLight const> activeEnvMap_;
    ImageAssetPool imageAssetPool_;
    kira::FileResolver fileResolver_;
    /// Next context ID. Transactions on different threads allocate IDs concurrently.
    std::atomic_size_t nextId_{0};

    /// IDs of objects created this epoch and still in the scene. A created object is born clean,
    /// so it enters \c changedIds_ only when a setter changes it.
    ///
    /// Every ID in this set and in \c changedIds_ is in \c objects_. A removed ID appears only in
    /// \c removedIds_.
    std::unordered_set<std::size_t> addedIds_;

    /// IDs of objects that recorded a bit this epoch and are still in the scene. An object enters
    /// when its first bit is recorded, so each is listed once.
    std::unordered_set<std::size_t> changedIds_;

    /// IDs of objects removed this epoch. Each ID keeps its index entry until \c clearDirty.
    std::unordered_set<std::size_t> removedIds_;
    DirtyBits dirtyBits_{DirtyBits::None};
    std::uint64_t epoch_{0};
};

template <> inline constexpr bool isEnumFlags<Context::DirtyBits> = true;
} // namespace flux
