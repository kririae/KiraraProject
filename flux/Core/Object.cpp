#include "flux/Core/Object.h"

#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"

namespace flux {
ContextObject::ContextObject(TXContext &tx)
    : context_(&tx.getContext()), contextId_(tx.allocateId()) {}

void ContextObject::registerTo(TXContext &tx) { tx.registerObject(Ref<ContextObject>{this}); }

void ContextObject::recordChanged() { context_->changedIds_.push_back(contextId_); }

ConfigurableObject::ConfigurableObject(TXContext &tx) : ContextObject(tx) {}
} // namespace flux
