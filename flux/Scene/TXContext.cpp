#include "flux/Scene/TXContext.h"

#include "flux/Scene/Context.h"

namespace flux {
std::size_t TXContext::allocateId() { return context_->allocateId(); }

void TXContext::registerObject(Ref<ContextObject> object) {
    if (object->getContext() != context_)
        throw kira::Anyhow("TXContext: object belongs to another context");

    auto const [unused, inserted] = objects_.emplace(object->getContextId(), std::move(object));
    if (!inserted)
        throw kira::Anyhow("TXContext: object ID is already registered");
}

void TXContext::stageForLink(std::size_t contextId) { stagedForLink_.push_back(contextId); }

void TXContext::stageActiveSampler(std::size_t contextId) noexcept { activeSamplerId_ = contextId; }
} // namespace flux
