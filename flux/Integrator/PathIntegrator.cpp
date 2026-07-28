#include "flux/Integrator/PathIntegrator.h"

#include <utility>

#include "flux/Scene/TXContext.h"

namespace flux {
PathIntegrator::PathIntegrator(TXContext &tx, kira::Properties properties)
    : RenderObject(tx, std::move(properties)) {}

void PathIntegrator::registerTo(TXContext &tx) {
    RenderObject::registerTo(tx);
    tx.stageActiveIntegrator(getContextId());
}
} // namespace flux
