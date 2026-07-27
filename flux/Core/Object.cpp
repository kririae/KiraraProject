#include "flux/Core/Object.h"

#include "flux/Scene/TXContext.h"

namespace flux {
ContextObject::ContextObject(TXContext &tx)
    : context_(&tx.getContext()), contextId_(tx.allocateId()) {}

void ContextObject::registerTo(TXContext &tx) { tx.registerObject(Ref<ContextObject>{this}); }

ConfigurableObject::ConfigurableObject(TXContext &tx, kira::Properties properties)
    : ContextObject(tx), properties_(std::move(properties)) {}
} // namespace flux
