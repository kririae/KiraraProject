#include "flux/Shading/BSDF.h"

#include <cmath>
#include <string>

#include "flux/Core/KIRA.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
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
    throw kira::Anyhow("BSDF: type must be 'diffuse' or 'principled', got '{}'", type);
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
    R_ = Texture::resolve(tx, props, "R", Spectrum{0.5F});
}

DiffuseBSDF::Impl DiffuseBSDF::getImpl() const noexcept { return {.R = R_->getImpl()}; }

PrincipledBSDF::PrincipledBSDF(TXContext &tx, kira::Properties const &props)
    : BSDF(tx, BSDFType::Principled) {
    baseColor_ = Texture::resolve(tx, props, "base_color", Spectrum{0.5F});
    roughness_ = Texture::resolve(tx, props, "roughness", 0.5F);
    metallic_ = Texture::resolve(tx, props, "metallic", 0.0F);
    specTrans_ = Texture::resolve(tx, props, "spec_trans", 0.0F);
    specTint_ = Texture::resolve(tx, props, "spec_tint", 0.0F);
    sheen_ = Texture::resolve(tx, props, "sheen", 0.0F);
    sheenTint_ = Texture::resolve(tx, props, "sheen_tint", 0.0F);
    flatness_ = Texture::resolve(tx, props, "flatness", 0.0F);
    clearcoat_ = Texture::resolve(tx, props, "clearcoat", 0.0F);
    clearcoatRoughness_ = Texture::resolve(tx, props, "clearcoat_roughness", 1.0F);

    if (props.contains("eta") && props.contains("specular"))
        throw kira::Anyhow("PrincipledBSDF: specify either eta or specular");
    if (props.contains("eta"))
        eta_ = props.use<float>("eta");
    else
        eta_ = etaFromSpecular(props.use_or<float>("specular", 0.5F));
}

PrincipledBSDF::Impl PrincipledBSDF::getImpl() const noexcept {
    return {
        .baseColor = baseColor_->getImpl(),
        .roughness = roughness_->getImpl(),
        .metallic = metallic_->getImpl(),
        .specTrans = specTrans_->getImpl(),
        .specTint = specTint_->getImpl(),
        .sheen = sheen_->getImpl(),
        .sheenTint = sheenTint_->getImpl(),
        .flatness = flatness_->getImpl(),
        .clearcoat = clearcoat_->getImpl(),
        .clearcoatRoughness = clearcoatRoughness_->getImpl(),
        .eta = eta_,
    };
}
} // namespace flux
