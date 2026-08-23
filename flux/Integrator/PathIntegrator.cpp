#include "flux/Integrator/PathIntegrator.h"

#include <string>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
PathIntegrator::PathIntegrator(TXContext &tx, kira::Properties const &props)
    : RenderObject(tx),
      impl_{
          props.use_or<std::uint32_t>("max_depth", 8),
          props.use_or<std::uint32_t>("rr_depth", 2),
          props.use_or<float>("rr_prob", 0.95F),
      },
      shaderReorder_(props.use_or<bool>("shader_reorder", true)) {
    auto const type = props.use_or<std::string>("type", "path");
    if (type != "path")
        throw kira::Anyhow("PathIntegrator: unsupported type '{}'", type);

    if (impl_.maxDepth == 0)
        throw kira::Anyhow("PathIntegrator: maximum depth must be nonzero");
    if (!(impl_.rrProb > 0.0F && impl_.rrProb <= 1.0F))
        throw kira::Anyhow(
            "PathIntegrator: Russian roulette probability must be greater than zero and at most one"
        );
}

void PathIntegrator::registerTo(TXContext &tx) {
    RenderObject::registerTo(tx);
    tx.stageActiveIntegrator(getContextId());
}
} // namespace flux
