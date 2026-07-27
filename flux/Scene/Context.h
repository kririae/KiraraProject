#pragma once

#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"
#include "kira/SmallVector.h"

namespace flux {
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
    /// none of its objects.
    template <IsConfigurableObject T>
    [[nodiscard]] Ref<T> create(kira::Properties properties = {}) {
        if (committing_)
            throw kira::Anyhow("Context: object creation is not allowed during commit");

        TXContext tx(*this);
        auto object = tx.create<T>(std::move(properties));
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

    /// \brief Links every object added since the last successful commit.
    ///
    /// A failed batch remains staged so the caller can fix external state and
    /// retry it.
    void commit();

private:
    Context() = default;

    [[nodiscard]] std::size_t allocateId() noexcept { return nextId_++; }
    void absorb(TXContext &&tx);

    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    kira::SmallVector<std::size_t> stagedForLink_;
    std::size_t nextId_{0};
    bool committing_{false};
};
} // namespace flux
