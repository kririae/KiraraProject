#include "flux/Sampling/Sampler.h"

#include <string>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
Ref<Sampler> Sampler::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "independent");
    if (type == "independent")
        return tx.create<IndependentSampler>(props);
    if (type == "sobol")
        return tx.create<SobolSampler>(props);
    throw kira::Anyhow("Sampler: unsupported type '{}'", type);
}

Sampler::Sampler(TXContext &tx, SamplerType type) : RenderObject(tx), type_(type) {}

IndependentSampler::IndependentSampler(TXContext &tx, kira::Properties const &)
    : Sampler(tx, SamplerType::Independent) {}

Sampler::Impl Sampler::getImpl() const {
    auto const type = getType();
    switch (type) {
    case SamplerType::Independent:
        return {
            .type = type,
            .storage = {
                .independent = static_cast<IndependentSampler const &>(*this).getImpl(),
            },
        };
    case SamplerType::Sobol:
        return {
            .type = type,
            .storage = {
                .sobol = static_cast<SobolSampler const &>(*this).getImpl(),
            },
        };
    }
    KIRA_UNREACHABLE();
}

IndependentSampler::Impl IndependentSampler::getImpl() const noexcept { return {}; }
SobolSampler::SobolSampler(TXContext &tx, kira::Properties const &)
    : Sampler(tx, SamplerType::Sobol) {}

SobolSampler::Impl SobolSampler::getImpl() const noexcept { return {}; }
} // namespace flux
