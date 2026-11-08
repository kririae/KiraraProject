#include "flux/Shading/EDF.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "flux/Core/Logging.h"
#include "flux/Core/MathUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"
#include "flux/Shading/ShaderTextures.h"
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

ConstantEDF::ConstantEDF(TXContext &tx, kira::Properties const &props)
    : EDF(tx, EDFType::Constant) {
    radiance_ = boundRadiance(props.use_or<Spectrum>("radiance", radiance_));
}

void ConstantEDF::setRadiance(Spectrum const &radiance) {
    setIfDifferent(radiance_, boundRadiance(radiance), DirtyBits::Radiance);
}

EDF::Impl ConstantEDF::getImpl() const {
    return {
        .type = EDFType::Constant,
        .storage = {.constant = {.radiance = radiance_}},
    };
}

float ConstantEDF::estimateLuminance() const noexcept { return luminance(radiance_); }

ShaderEDF::ShaderEDF(
    TXContext &tx, ShaderAssetSpec const &spec, std::vector<std::uint32_t> words,
    std::vector<Ref<ImageTexture const>> textures
)
    : EDF(tx, EDFType::Shader), asset_(tx.getShaderAssetPool().getOrCreate(spec)),
      words_(std::move(words)), textures_(std::move(textures)) {
    checkShaderTextures(textures_, [&](ImageTexture const &texture) {
        return tx.contains(texture);
    });
}

void ShaderEDF::setShader(
    ShaderAssetSpec const &spec, std::vector<std::uint32_t> words,
    std::vector<Ref<ImageTexture const>> textures
) {
    auto *const context = getContext();
    if (!context)
        throw std::invalid_argument("ShaderEDF: the EDF is not in a context");
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

EDF::Impl ShaderEDF::getImpl() const {
    return {
        .type = EDFType::Shader,
        .storage = {.shader = {.index = getContext()->getIndex<EDF>(getContextId())}},
    };
}
} // namespace flux
