#include "flux/Shading/EDF.h"

#include <cmath>
#include <string>

#include "flux/Core/MathUtils.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
void validateRadiance(Spectrum const &radiance) {
    for (auto const value : radiance)
        if (!(value >= 0.0F && std::isfinite(value)))
            throw kira::Anyhow("ConstantEDF: radiance must be finite and nonnegative");
}
} // namespace

Ref<EDF> EDF::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "constant");
    if (type == "constant")
        return tx.create<ConstantEDF>(props);
    throw kira::Anyhow("EDF: type must be 'constant', got '{}'", type);
}

ConstantEDF::ConstantEDF(TXContext &tx, kira::Properties const &props) : EDF(tx) {
    radiance_ = props.use_or<Spectrum>("radiance", radiance_);
    validateRadiance(radiance_);
}

void ConstantEDF::setRadiance(Spectrum const &radiance) {
    validateRadiance(radiance);
    radiance_ = radiance;
}

EDF::Impl ConstantEDF::getImpl() const { return Impl{.radiance = radiance_}; }

float ConstantEDF::estimateLuminance() const noexcept { return luminance(radiance_); }
} // namespace flux
