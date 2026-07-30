#include "flux/Shading/BSDF.h"

#include <cmath>
#include <string>

#include "flux/Core/KIRA.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
void validateReflectance(Spectrum const &reflectance) {
    for (auto const value : reflectance)
        if (!(value >= 0.0F && value <= 1.0F && std::isfinite(value)))
            throw kira::Anyhow("DiffuseBSDF: reflectance must be finite and between zero and one");
}
} // namespace

Ref<BSDF> BSDF::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use<std::string>("type");
    if (type == "diffuse")
        return tx.create<DiffuseBSDF>(props);
    throw kira::Anyhow("BSDF: unsupported type '{}'", type);
}

BSDF::BSDF(TXContext &tx, BSDFType type) : RenderObject(tx), type_(type) {}

BSDF::Impl BSDF::getImpl() const {
    switch (type_) {
    case BSDFType::Diffuse: return static_cast<DiffuseBSDF const &>(*this).getImpl();
    case BSDFType::Count: break;
    }
    KIRA_UNREACHABLE();
}

DiffuseBSDF::DiffuseBSDF(TXContext &tx, kira::Properties const &props)
    : BSDF(tx, BSDFType::Diffuse) {
    reflectance_ = props.use_or<Spectrum>("R", reflectance_);
    validateReflectance(reflectance_);
}

DiffuseBSDF::Impl DiffuseBSDF::getImpl() const noexcept { return {.reflectance = reflectance_}; }
} // namespace flux
