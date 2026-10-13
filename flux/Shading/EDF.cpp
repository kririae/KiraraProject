#include "flux/Shading/EDF.h"

#include <string>

#include "flux/Core/Logging.h"
#include "flux/Core/MathUtils.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] Spectrum boundRadiance(Spectrum radiance) {
    for (auto &value : radiance)
        if (value < 0.0F) {
            LogWarn("ConstantEDF: radiance component {} is negative, using 0", value);
            value = 0.0F;
        }
    return radiance;
}
} // namespace

Ref<EDF> EDF::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "constant");
    if (type == "constant")
        return tx.create<ConstantEDF>(props);
    throw kira::Anyhow("EDF: type must be 'constant', got '{}'", type);
}

ConstantEDF::ConstantEDF(TXContext &tx, kira::Properties const &props) : EDF(tx) {
    radiance_ = boundRadiance(props.use_or<Spectrum>("radiance", radiance_));
}

void ConstantEDF::setRadiance(Spectrum const &radiance) {
    setIfDifferent(radiance_, boundRadiance(radiance), DirtyBits::Radiance);
}

EDF::Impl ConstantEDF::getImpl() const { return Impl{.radiance = radiance_}; }

float ConstantEDF::estimateLuminance() const noexcept { return luminance(radiance_); }
} // namespace flux
