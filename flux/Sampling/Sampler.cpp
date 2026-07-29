#include "flux/Sampling/Sampler.h"

#include <utility>

#include "flux/Scene/TXContext.h"

namespace flux {
Sampler::Sampler(TXContext &tx, kira::Properties properties, SamplerType type)
    : RenderObject(tx, std::move(properties)), type_(type) {}

void Sampler::registerTo(TXContext &tx) {
    RenderObject::registerTo(tx);
    tx.stageActiveSampler(getContextId());
}

IndependentSampler::IndependentSampler(TXContext &tx, kira::Properties properties)
    : Sampler(tx, std::move(properties), SamplerType::Independent) {}

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
