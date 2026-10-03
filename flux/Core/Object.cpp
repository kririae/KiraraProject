#include "flux/Core/Object.h"

#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"

namespace flux {
ContextObject::ContextObject(TXContext &tx) : contextId_(tx.allocateId()) {}

void ContextObject::recordDirtied() {
    std::scoped_lock const lock(context_->mutex_);
    context_->dirtiedIds_.insert(contextId_);
}

void ContextObject::bumpContextVersion() noexcept { context_->bumpVersion(); }

ConfigurableObject::ConfigurableObject(TXContext &tx) : ContextObject(tx) {}
} // namespace flux
