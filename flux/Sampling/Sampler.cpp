#include "flux/Sampling/Sampler.h"

#include <cstdint>
#include <string>

#include "flux/Core/Logging.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
/// Returns \p offset if it is nonnegative, otherwise reports it and returns 0.
[[nodiscard]] std::uint64_t boundSampleOffset(std::int64_t offset) {
    if (offset >= 0)
        return static_cast<std::uint64_t>(offset);
    LogWarn("Sampler: sample offset {} is negative, using 0", offset);
    return 0;
}
} // namespace

Ref<Sampler> Sampler::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "independent");
    if (type == "independent")
        return tx.create<IndependentSampler>(props);
    if (type == "sobol")
        return tx.create<SobolSampler>(props);
    throw kira::Anyhow("Sampler: unsupported type '{}'", type);
}

Sampler::Sampler(TXContext &tx, SamplerType type, kira::Properties const &props)
    : RenderObject(tx), type_(type),
      sampleOffset_(boundSampleOffset(props.use_or<std::int64_t>("sample_offset", 0))) {}

void Sampler::setSampleOffset(std::uint64_t offset) {
    setIfDifferent(sampleOffset_, offset, DirtyBits::SampleOffset);
}

IndependentSampler::IndependentSampler(TXContext &tx, kira::Properties const &props)
    : Sampler(tx, SamplerType::Independent, props) {}

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
SobolSampler::SobolSampler(TXContext &tx, kira::Properties const &props)
    : Sampler(tx, SamplerType::Sobol, props) {}

SobolSampler::Impl SobolSampler::getImpl() const noexcept { return {}; }
} // namespace flux
