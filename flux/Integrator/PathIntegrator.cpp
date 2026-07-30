#include "flux/Integrator/PathIntegrator.h"

#include <string>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
PathIntegrator::PathIntegrator(TXContext &tx, kira::Properties const &props) : RenderObject(tx) {
    auto const type = props.use_or<std::string>("type", "path");
    if (type != "path")
        throw kira::Anyhow("PathIntegrator: unsupported type '{}'", type);
}

void PathIntegrator::registerTo(TXContext &tx) {
    RenderObject::registerTo(tx);
    tx.stageActiveIntegrator(getContextId());
}
} // namespace flux
