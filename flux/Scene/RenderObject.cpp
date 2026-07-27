#include "flux/Scene/RenderObject.h"

#include "flux/Scene/TXContext.h"

namespace flux {
RenderObject::RenderObject(TXContext &tx, kira::Properties properties)
    : ConfigurableObject(tx, std::move(properties)) {}

void RenderObject::registerTo(TXContext &tx) {
    ConfigurableObject::registerTo(tx);
    tx.stageForLink(getContextId());
}
} // namespace flux
