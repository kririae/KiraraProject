#include "flux/Integrator/PathIntegrator.h"

#include <string>

#include "flux/Core/Logging.h"
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
      shaderReorder_(props.use_or<bool>("shader_reorder", false)),
      chunkSize_(props.use_or<std::uint32_t>("chunk_size", 1)) {
    auto const type = props.use_or<std::string>("type", "path");
    if (type != "path")
        throw kira::Anyhow("PathIntegrator: unsupported type '{}'", type);

    if (chunkSize_ == 0) {
        LogWarn("PathIntegrator: chunk size is 0, using 1");
        chunkSize_ = 1;
    }
    if (impl_.rrProb < 0.0F) {
        LogWarn(
            "PathIntegrator: Russian roulette probability {} is below 0, using 0", impl_.rrProb
        );
        impl_.rrProb = 0.0F;
    } else if (impl_.rrProb > 1.0F) {
        LogWarn(
            "PathIntegrator: Russian roulette probability {} is above 1, using 1", impl_.rrProb
        );
        impl_.rrProb = 1.0F;
    }
}
} // namespace flux
