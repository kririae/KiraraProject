#include "flux/Scene/TXContext.h"

#include "flux/Scene/Context.h"

namespace flux {
std::size_t TXContext::allocateId() { return context_->allocateId(); }

Ref<ContextObject> TXContext::getObject(std::size_t contextId) const {
    if (auto const iterator = objects_.find(contextId); iterator != objects_.end())
        return iterator->second;
    return context_->get<ContextObject>(contextId);
}

void TXContext::registerObject(Ref<ContextObject> object) {
    if (object->getContext() != context_)
        throw kira::Anyhow("TXContext: object belongs to another context");

    auto const [unused, inserted] = objects_.emplace(object->getContextId(), std::move(object));
    if (!inserted)
        throw kira::Anyhow("TXContext: object ID is already registered");
}

void TXContext::stageActiveIntegrator(std::size_t contextId) noexcept {
    if (!activeIntegratorId_)
        activeIntegratorId_ = contextId;
}

void TXContext::stageActiveSampler(std::size_t contextId) noexcept {
    if (!activeSamplerId_)
        activeSamplerId_ = contextId;
}
} // namespace flux
