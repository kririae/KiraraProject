#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>

#include "flux/Core/Object.h"
#include "kira/SmallVector.h"

namespace flux {
class RenderObject;
class Sampler;

/// \brief Collects objects created by one \c Context::create call.
///
/// A transaction belongs to one context. The context absorbs it only after the
/// requested object has been fully constructed and registered.
class TXContext {
    friend class Context;
    friend class ContextObject;
    friend class RenderObject;
    friend class Sampler;

public:
    /// \brief Creates and registers a configurable object in this transaction.
    ///
    /// \c T must grant \c TXContext access to its constructor.
    template <IsConfigurableObject T>
    [[nodiscard]] Ref<T> create(kira::Properties properties = {}) {
        Ref<T> object{new T(*this, std::move(properties))};
        static_cast<ContextObject *>(object.get())->registerTo(*this);
        return object;
    }

private:
    explicit TXContext(Context &context) noexcept : context_(&context) {}

    [[nodiscard]] Context &getContext() const noexcept { return *context_; }
    [[nodiscard]] std::size_t allocateId();
    void registerObject(Ref<ContextObject> object);
    void stageForLink(std::size_t contextId);
    void stageActiveSampler(std::size_t contextId) noexcept;

    Context *context_;
    std::unordered_map<std::size_t, Ref<ContextObject>> objects_;
    kira::SmallVector<std::size_t> stagedForLink_;
    std::optional<std::size_t> activeSamplerId_;
};
} // namespace flux
