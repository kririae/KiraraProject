#include "flux/Shading/Texture.h"

#include <filesystem>
#include <string>

#include "flux/Scene/Context.h"
#include "flux/Scene/ImageAsset.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] ImageTextureAddressMode parseAddressMode(kira::Properties const &props) {
    auto const value = props.use_or<std::string>("address_mode", "wrap");
    if (value == "wrap")
        return ImageTextureAddressMode::Wrap;
    if (value == "clamp")
        return ImageTextureAddressMode::Clamp;
    if (value == "mirror")
        return ImageTextureAddressMode::Mirror;
    if (value == "border")
        return ImageTextureAddressMode::Border;
    throw kira::Anyhow("ImageTexture: unsupported address mode '{}'", value);
}

[[nodiscard]] ImageTextureFilterMode parseFilterMode(kira::Properties const &props) {
    auto const value = props.use_or<std::string>("filter_mode", "linear");
    if (value == "linear")
        return ImageTextureFilterMode::Linear;
    if (value == "point")
        return ImageTextureFilterMode::Point;
    throw kira::Anyhow("ImageTexture: unsupported filter mode '{}'", value);
}

} // namespace

Ref<Texture> Texture::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "constant");
    if (type == "constant")
        return tx.create<ConstantTexture>(props);
    if (type == "image")
        return tx.create<ImageTexture>(props);
    throw kira::Anyhow("Texture: type must be 'constant' or 'image', got '{}'", type);
}

Ref<Texture const>
Texture::resolve(TXContext &tx, kira::Properties const &parent, std::string_view name) {
    kira::Properties props;
    if (parent.is_type_of<float>(name))
        props.set("value", parent.use<float>(name));
    else if (parent.is_type_of<Spectrum>(name))
        props.set("value", parent.use<Spectrum>(name));
    else if (parent.is_type_of<kira::Properties>(name))
        return tx.create<Texture>(parent.use_view(name));
    else if (parent.contains(name))
        throw kira::Anyhow("Texture: '{}' must be a scalar, color, or inline table", name);
    else
        throw kira::Anyhow("Texture: expected property '{}'", name);
    return tx.create<ConstantTexture>(props);
}

Ref<Texture const> Texture::resolve(
    TXContext &tx, kira::Properties const &parent, std::string_view name, float defaultValue
) {
    if (parent.contains(name))
        return resolve(tx, parent, name);
    kira::Properties defaults;
    defaults.set(name, defaultValue);
    return resolve(tx, defaults, name);
}

Ref<Texture const> Texture::resolve(
    TXContext &tx, kira::Properties const &parent, std::string_view name,
    Spectrum const &defaultValue
) {
    if (parent.contains(name))
        return resolve(tx, parent, name);
    kira::Properties defaults;
    defaults.set(name, defaultValue);
    return resolve(tx, defaults, name);
}

ConstantTexture::ConstantTexture(TXContext &tx, kira::Properties const &props) : Texture(tx) {
    if (props.is_type_of<float>("value"))
        value_ = Spectrum{props.use<float>("value")};
    else
        value_ = props.use<Spectrum>("value");
}

Texture::Impl ConstantTexture::getImpl() const {
    return {
        .type = TextureType::Constant,
        .storage = {.constant = Impl{value_}},
    };
}

ImageTexture::ImageTexture(TXContext &tx, kira::Properties const &props)
    : Texture(tx), addressMode_(parseAddressMode(props)), filterMode_(parseFilterMode(props)) {
    auto const inputPath = props.use<std::filesystem::path>("path");
    auto const path = tx.getContext().getFileResolver().resolve(inputPath);
    auto const colorSpace = props.use_or<std::string>("color_space", "linear");
    auto imageColorSpace = ImageColorSpace::Linear;
    if (colorSpace == "srgb")
        imageColorSpace = ImageColorSpace::SRGB;
    else if (colorSpace != "linear")
        throw kira::Anyhow("ImageTexture: unsupported color space '{}'", colorSpace);
    imageAsset_ = tx.getContext().getImageAssetPool().getOrCreate({
        .path = path,
        .colorSpace = imageColorSpace,
    });
    componentMapping_ = imageAsset_->getDefaultComponentMapping();
    if (props.contains("component_mapping")) {
        auto const value = props.use<std::string>("component_mapping");
        auto const parseSource = [](char source) {
            if (source == 'x')
                return ImageComponentSource::X;
            if (source == 'y')
                return ImageComponentSource::Y;
            if (source == 'z')
                return ImageComponentSource::Z;
            if (source == 'w')
                return ImageComponentSource::W;
            if (source == '0')
                return ImageComponentSource::Zero;
            if (source == '1')
                return ImageComponentSource::One;
            throw kira::Anyhow("ImageTexture: invalid component mapping '{}'", source);
        };
        if (value.size() != 4)
            throw kira::Anyhow("ImageTexture: component mapping must contain four selectors");
        componentMapping_ = {
            .r = parseSource(value[0]),
            .g = parseSource(value[1]),
            .b = parseSource(value[2]),
            .a = parseSource(value[3]),
        };
        if (!componentMapping_.isValid(imageAsset_->getComponentCount()))
            throw kira::Anyhow("ImageTexture: component mapping references a missing component");
    }
}

Texture::Impl ImageTexture::getImpl() const {
    return {
        .type = TextureType::Image,
        .storage = {
            .image = {.imageTextureIndex = getContext()->getImageTextureIndex(getContextId())}
        },
    };
}
} // namespace flux
