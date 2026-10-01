#include "flux/Core/Object.h"

#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"

namespace flux {
ContextObject::ContextObject(TXContext &tx) : contextId_(tx.allocateId()) {}

void ContextObject::recordChanged() {
    std::scoped_lock const lock(context_->mutex_);
    context_->changedIds_.insert(contextId_);
}

ConfigurableObject::ConfigurableObject(TXContext &tx) : ContextObject(tx) {}
} // namespace flux
