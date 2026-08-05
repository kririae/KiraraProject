#include "flux/Shading/BSDF.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "flux/Core/KIRA.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
// Read exponent bits so validation remains valid under fast-math.
[[nodiscard]] bool isFinite(float value) noexcept {
    constexpr std::uint32_t exponent = 0x7f800000U;
    return (std::bit_cast<std::uint32_t>(value) & exponent) != exponent;
}

void validateUnitSpectrum(Spectrum const &value, std::string_view name) {
    for (auto const component : value)
        if (!(component >= 0.0F && component <= 1.0F && isFinite(component)))
            throw kira::Anyhow("{} must be finite and between zero and one", name);
}

void validateUnitValue(float value, std::string_view name) {
    if (!(value >= 0.0F && value <= 1.0F && isFinite(value)))
        throw kira::Anyhow("{} must be finite and between zero and one", name);
}

[[nodiscard]] float etaFromSpecular(float specular) noexcept {
    return 2.0F / (1.0F - std::sqrt(0.08F * specular)) - 1.0F;
}
} // namespace

Ref<BSDF> BSDF::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use<std::string>("type");
    if (type == "diffuse")
        return tx.create<DiffuseBSDF>(props);
    if (type == "principled")
        return tx.create<PrincipledBSDF>(props);
    throw kira::Anyhow("BSDF: unsupported type '{}'", type);
}

BSDF::BSDF(TXContext &tx, BSDFType type) : RenderObject(tx), type_(type) {}

BSDF::Impl BSDF::getImpl() const {
    switch (type_) {
    case BSDFType::Diffuse:
        return {
            .type = type_,
            .storage = {.diffuse = static_cast<DiffuseBSDF const &>(*this).getImpl()},
        };
    case BSDFType::Principled:
        return {
            .type = type_,
            .storage = {.principled = static_cast<PrincipledBSDF const &>(*this).getImpl()},
        };
    case BSDFType::Count: break;
    }
    KIRA_UNREACHABLE();
}

DiffuseBSDF::DiffuseBSDF(TXContext &tx, kira::Properties const &props)
    : BSDF(tx, BSDFType::Diffuse) {
    reflectance_ = props.use_or<Spectrum>("R", reflectance_);
    validateUnitSpectrum(reflectance_, "DiffuseBSDF: R");
}

DiffuseBSDF::Impl DiffuseBSDF::getImpl() const noexcept { return {.reflectance = reflectance_}; }

PrincipledBSDF::PrincipledBSDF(TXContext &tx, kira::Properties const &props)
    : BSDF(tx, BSDFType::Principled) {
    baseColor_ = props.use_or<Spectrum>("base_color", baseColor_);
    roughness_ = props.use_or<float>("roughness", roughness_);
    metallic_ = props.use_or<float>("metallic", metallic_);
    specTrans_ = props.use_or<float>("spec_trans", specTrans_);
    specTint_ = props.use_or<float>("spec_tint", specTint_);
    sheen_ = props.use_or<float>("sheen", sheen_);
    sheenTint_ = props.use_or<float>("sheen_tint", sheenTint_);
    flatness_ = props.use_or<float>("flatness", flatness_);
    clearcoat_ = props.use_or<float>("clearcoat", clearcoat_);
    clearcoatRoughness_ = props.use_or<float>("clearcoat_roughness", clearcoatRoughness_);

    validateUnitSpectrum(baseColor_, "PrincipledBSDF: base_color");
    validateUnitValue(roughness_, "PrincipledBSDF: roughness");
    validateUnitValue(metallic_, "PrincipledBSDF: metallic");
    validateUnitValue(specTrans_, "PrincipledBSDF: spec_trans");
    validateUnitValue(specTint_, "PrincipledBSDF: spec_tint");
    validateUnitValue(sheen_, "PrincipledBSDF: sheen");
    validateUnitValue(sheenTint_, "PrincipledBSDF: sheen_tint");
    validateUnitValue(flatness_, "PrincipledBSDF: flatness");
    validateUnitValue(clearcoat_, "PrincipledBSDF: clearcoat");
    validateUnitValue(clearcoatRoughness_, "PrincipledBSDF: clearcoat_roughness");

    if (props.contains("eta") && props.contains("specular"))
        throw kira::Anyhow("PrincipledBSDF: specify either eta or specular");
    if (props.contains("eta")) {
        eta_ = props.use<float>("eta");
        if (!(eta_ > 0.0F && isFinite(eta_)))
            throw kira::Anyhow("PrincipledBSDF: eta must be finite and greater than zero");
    } else {
        auto specular = props.use_or<float>("specular", 0.5F);
        validateUnitValue(specular, "PrincipledBSDF: specular");
        if (specTrans_ > 0.0F && specular == 0.0F) {
            specular = 0.001F;
            LogWarn(
                "PrincipledBSDF: specular=0 is degenerate for transmission; using {}", specular
            );
        }
        eta_ = etaFromSpecular(specular);
    }
    if (specTrans_ > 0.0F && eta_ == 1.0F) {
        eta_ = 1.001F;
        LogWarn("PrincipledBSDF: eta=1 is degenerate for transmission; using {}", eta_);
    }
}

PrincipledBSDF::Impl PrincipledBSDF::getImpl() const noexcept {
    return {
        .baseColor = baseColor_,
        .roughness = roughness_,
        .metallic = metallic_,
        .specTrans = specTrans_,
        .specTint = specTint_,
        .sheen = sheen_,
        .sheenTint = sheenTint_,
        .flatness = flatness_,
        .clearcoat = clearcoat_,
        .clearcoatRoughness = clearcoatRoughness_,
        .eta = eta_,
    };
}
} // namespace flux
