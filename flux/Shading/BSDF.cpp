#include "flux/Shading/BSDF.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "flux/Core/Logging.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/ShaderTextures.h"
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
    case BSDFType::Diffuse: {
        auto const impl = static_cast<DiffuseBSDF const &>(*this).getImpl();
        return {
            .type = type_,
            .storage = {.diffuse = impl},
        };
    }
    case BSDFType::Principled: {
        auto const impl = static_cast<PrincipledBSDF const &>(*this).getImpl();
        return {
            .type = type_,
            .storage = {.principled = impl},
        };
    }
    case BSDFType::Shader: {
        auto const impl = static_cast<ShaderBSDF const &>(*this).getImpl();
        return {
            .type = type_,
            .storage = {.shader = impl},
        };
    }
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

void PrincipledBSDF::replaceTexture(
    Ref<Texture const> &property, Ref<Texture const> texture, DirtyBits bits
) {
    if (!texture)
        throw std::invalid_argument("PrincipledBSDF: texture must not be null");
    if (!getContext() || texture->getContext() != getContext())
        throw std::invalid_argument("PrincipledBSDF: texture belongs to another context");
    setIfDifferent(property, texture, bits);
}

void PrincipledBSDF::setBaseColor(Ref<Texture const> texture) {
    replaceTexture(baseColor_, std::move(texture), DirtyBits::BaseColor);
}

void PrincipledBSDF::setRoughness(Ref<Texture const> texture) {
    replaceTexture(roughness_, std::move(texture), DirtyBits::Roughness);
}

void PrincipledBSDF::setMetallic(Ref<Texture const> texture) {
    replaceTexture(metallic_, std::move(texture), DirtyBits::Metallic);
}

void PrincipledBSDF::setSpecTrans(Ref<Texture const> texture) {
    replaceTexture(specTrans_, std::move(texture), DirtyBits::SpecTrans);
}

void PrincipledBSDF::setSpecTint(Ref<Texture const> texture) {
    replaceTexture(specTint_, std::move(texture), DirtyBits::SpecTint);
}

void PrincipledBSDF::setSheen(Ref<Texture const> texture) {
    replaceTexture(sheen_, std::move(texture), DirtyBits::Sheen);
}

void PrincipledBSDF::setSheenTint(Ref<Texture const> texture) {
    replaceTexture(sheenTint_, std::move(texture), DirtyBits::SheenTint);
}

void PrincipledBSDF::setFlatness(Ref<Texture const> texture) {
    replaceTexture(flatness_, std::move(texture), DirtyBits::Flatness);
}

void PrincipledBSDF::setClearcoat(Ref<Texture const> texture) {
    replaceTexture(clearcoat_, std::move(texture), DirtyBits::Clearcoat);
}

void PrincipledBSDF::setClearcoatRoughness(Ref<Texture const> texture) {
    replaceTexture(clearcoatRoughness_, std::move(texture), DirtyBits::ClearcoatRoughness);
}

void PrincipledBSDF::setEta(float eta) { setIfDifferent(eta_, eta, DirtyBits::Eta); }

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

ShaderBSDF::ShaderBSDF(
    TXContext &tx, ShaderAssetSpec const &spec, std::vector<std::uint32_t> words,
    std::vector<Ref<ImageTexture const>> textures
)
    : BSDF(tx, BSDFType::Shader), asset_(tx.getShaderAssetPool().getOrCreate(spec)),
      words_(std::move(words)), textures_(std::move(textures)) {
    checkShaderTextures(textures_, [&](ImageTexture const &texture) {
        return tx.contains(texture);
    });
}

void ShaderBSDF::setShader(
    ShaderAssetSpec const &spec, std::vector<std::uint32_t> words,
    std::vector<Ref<ImageTexture const>> textures
) {
    auto *const context = getContext();
    if (!context)
        throw std::invalid_argument("ShaderBSDF: the BSDF is not in a context");
    checkShaderTextures(textures, [&](ImageTexture const &texture) {
        return texture.getContext() == context;
    });

    auto asset = context->getShaderAssetPool().getOrCreate(spec);
    if (asset == asset_ && words == words_ && textures == textures_)
        return;

    markDirty(DirtyBits::Shader);
    asset_ = std::move(asset);
    words_ = std::move(words);
    textures_ = std::move(textures);
}

ShaderBSDF::Impl ShaderBSDF::getImpl() const {
    return {.index = getContext()->getIndex<BSDF>(getContextId())};
}
} // namespace flux
