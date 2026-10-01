#include "flux/Core/Object.h"

#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"

namespace flux {
ContextObject::ContextObject(TXContext &tx)
    : context_(&tx.getContext()), contextId_(tx.allocateId()) {}

void ContextObject::registerTo(TXContext &tx) { tx.registerObject(Ref<ContextObject>{this}); }

void ContextObject::recordChanged() {
    std::scoped_lock const lock(context_->mutex_);
    context_->changedIds_.insert(contextId_);
}

ConfigurableObject::ConfigurableObject(TXContext &tx) : ContextObject(tx) {}
} // namespace flux
