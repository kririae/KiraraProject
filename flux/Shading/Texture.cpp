#include "flux/Shading/Texture.h"

#include <string>

#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace flux {
Ref<Texture> Texture::create(TXContext &tx, kira::Properties const &props) {
    auto const type = props.use_or<std::string>("type", "constant");
    if (type == "constant")
        return tx.create<ConstantTexture>(props);
    throw kira::Anyhow("Texture: type must be 'constant', got '{}'", type);
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
} // namespace flux
