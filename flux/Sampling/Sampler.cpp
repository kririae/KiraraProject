#include "flux/Sampling/Sampler.h"

#include <string>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
Ref<Sampler> Sampler::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "independent");
    if (type == "independent")
        return tx.create<IndependentSampler>(props);
    throw kira::Anyhow("Sampler: unsupported type '{}'", type);
}

Sampler::Sampler(TXContext &tx, SamplerType type) : RenderObject(tx), type_(type) {}

void Sampler::registerTo(TXContext &tx) {
    RenderObject::registerTo(tx);
    tx.stageActiveSampler(getContextId());
}

IndependentSampler::IndependentSampler(TXContext &tx, kira::Properties const &)
    : Sampler(tx, SamplerType::Independent) {}

Sampler::Impl Sampler::getImpl(Vec2u const &resolution) const {
    auto const type = getType();
    switch (type) {
    case SamplerType::Independent:
        return {
            .type = type,
            .storage = {
                .independent = static_cast<IndependentSampler const &>(*this).getImpl(resolution),
            },
        };
    }
    KIRA_UNREACHABLE();
}

IndependentSampler::Impl
IndependentSampler::getImpl([[maybe_unused]] Vec2u const &resolution) const noexcept {
    return {};
}
} // namespace flux
