#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>

#include "flux/Core/Object.h"
#include "kira/Anyhow.h"

namespace flux {
class PathIntegrator;
class Sampler;

/// \brief Collects objects created by one \c Context::create call.
///
/// A transaction belongs to one context. The context absorbs it only after the
/// requested object has been fully constructed and registered.
class TXContext {
    friend class Context;
    friend class ContextObject;
    friend class PathIntegrator;
    friend class Sampler;

public:
    /// \brief Creates and registers a configurable object in this transaction.
    ///
    /// Base context object types provide a private \c create function that
    /// selects a concrete type inside this transaction. Concrete types grant
    /// \c TXContext access to their constructor.
    template <IsConfigurableObject T>
    [[nodiscard]] Ref<T> create(kira::Properties const &props = {}) {
        if constexpr (requires {
                          { T::create(*this, props) } -> std::same_as<Ref<T>>;
                      }) {
            return T::create(*this, props);
        } else {
            Ref<T> object{new T(*this, props)};
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
    explicit TXContext(Context &context) noexcept : context_(&context) {}

    [[nodiscard]] Context &getContext() const noexcept { return *context_; }
    [[nodiscard]] Ref<ContextObject> getObject(std::size_t contextId) const;
    [[nodiscard]] std::size_t allocateId();
    void registerObject(Ref<ContextObject> object);
    void stageActiveIntegrator(std::size_t contextId) noexcept;
    void stageActiveSampler(std::size_t contextId) noexcept;

    Context *context_;
    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    std::optional<std::size_t> activeIntegratorId_;
    std::optional<std::size_t> activeSamplerId_;
};
} // namespace flux
