#include "flux/Shading/BSDF.h"

#include <cmath>
#include <utility>

#include "flux/Core/KIRA.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
void validateReflectance(Spectrum const &reflectance) {
    for (auto const value : reflectance)
        if (!(value >= 0.0F && value <= 1.0F && std::isfinite(value)))
            throw kira::Anyhow("DiffuseBSDF: reflectance must be finite and between zero and one");
}
} // namespace

BSDF::BSDF(TXContext &tx, kira::Properties properties, BSDFType type)
    : RenderObject(tx, std::move(properties)), type_(type) {}

BSDF::Impl BSDF::getImpl() const {
    switch (type_) {
    case BSDFType::Diffuse: return static_cast<DiffuseBSDF const &>(*this).getImpl();
    case BSDFType::Count: break;
    }
    KIRA_UNREACHABLE();
}

DiffuseBSDF::DiffuseBSDF(TXContext &tx, kira::Properties properties)
    : BSDF(tx, std::move(properties), BSDFType::Diffuse) {
    reflectance_ = getProperties().use_or<Spectrum>("reflectance", reflectance_);
    validateReflectance(reflectance_);
}

DiffuseBSDF::Impl DiffuseBSDF::getImpl() const noexcept { return {.reflectance = reflectance_}; }
} // namespace flux
