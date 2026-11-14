#include "flux/Embree/EmbreeImageTexturePool.h"

#include <OpenImageIO/texture.h>

#include <utility>

#include "flux/Core/RayFootprint.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/ImageAssetPImpl.h"
#include "flux/Shading/Texture.h"

namespace flux {
struct EmbreeImageTexturePool::Entry {
    Ref<ImageAsset const> asset;
    OIIO::TextureOpt options;
    std::uint8_t sampleComponentCount{};
    std::uint8_t orientation{};
    ImageComponentMapping componentMapping;
    Vec4f scale;
    Vec4f bias;

    /// \brief Returns the value of \p texel.
    [[nodiscard]] Vec4f apply(Vec4f const &texel) const noexcept {
        auto const mapped =
            componentMapping.apply<Vec4f>(texel.x(), texel.y(), texel.z(), texel.w());
        return mapped * scale + bias;
    }
};

EmbreeImageTexturePool::EmbreeImageTexturePool() = default;
EmbreeImageTexturePool::~EmbreeImageTexturePool() = default;

void EmbreeImageTexturePool::build(Context const &context) {
    auto const textures = context.getObjects<ImageTexture>();
    auto const addressMode = [](ImageTextureAddressMode mode) {
        switch (mode) {
        case ImageTextureAddressMode::Wrap: return OIIO::Tex::Wrap::Periodic;
        case ImageTextureAddressMode::Clamp: return OIIO::Tex::Wrap::Clamp;
        case ImageTextureAddressMode::Mirror: return OIIO::Tex::Wrap::Mirror;
        case ImageTextureAddressMode::Border: return OIIO::Tex::Wrap::Black;
        }
        KIRA_UNREACHABLE();
    };

    textures_.clear();
    textures_.resize(context.getIndexLimit<ImageTexture>());

    for (auto const &texture : textures) {
        auto const &asset = texture->getImageAsset();
        auto options = OIIO::TextureOpt{};
        auto const &addressModes = texture->getAddressModes();
        auto const [sMode, tMode] = transposesAxes(asset->pImpl_->orientation)
                                        ? std::pair{addressModes[1], addressModes[0]}
                                        : std::pair{addressModes[0], addressModes[1]};
        options.swrap = addressMode(sMode);
        options.twrap = addressMode(tMode);
        options.mipmode = OIIO::Tex::MipMode::Aniso;
        options.anisotropic = RayFootprint::maxAnisotropy;
        options.interpmode = texture->getFilterMode() == ImageTextureFilterMode::Linear
                                 ? OIIO::Tex::InterpMode::Bilinear
                                 : OIIO::Tex::InterpMode::Closest;
        // OIIO converts color components before filtering.
        options.colortransformid = asset->pImpl_->colorTransformId;

        auto sampleComponentCount = asset->pImpl_->fileComponentCount;
        // Request RGBA for RGB images so OIIO filters the added alpha. Border
        // mode blends it with zero outside the image.
        if (sampleComponentCount == 3) {
            options.fill = 1.0F;
            sampleComponentCount = 4;
        }

        textures_[context.getIndex<ImageTexture>(texture->getContextId())] = {
            .asset = asset,
            .options = options,
            .sampleComponentCount = sampleComponentCount,
            .orientation = asset->pImpl_->orientation,
            .componentMapping = texture->getComponentMapping(),
            .scale = texture->getScale(),
            .bias = texture->getBias(),
        };
    }
}

void EmbreeImageTexturePool::clear() noexcept { textures_.clear(); }

Vec4f EmbreeImageTexturePool::Impl::eval4f(
    std::uint32_t index, Vec2f uv, Vec2f duvdx, Vec2f duvdy
) const noexcept {
    return eval4f(index, uv, duvdx, duvdy, false);
}

Vec4f EmbreeImageTexturePool::Impl::evalPoint4f(std::uint32_t index, Vec2f uv) const noexcept {
    return eval4f(index, uv, {}, {}, true);
}

Vec4f EmbreeImageTexturePool::Impl::eval4f(
    std::uint32_t index, Vec2f uv, Vec2f duvdx, Vec2f duvdy, bool point
) const noexcept {
    auto const &texture = textures[index];

    // Map oriented Flux UVs to the file coordinates used by OIIO.
    auto const orientGradient = [&](Vec2f &gradient) {
        switch (texture.orientation) {
        case 1: break;
        case 2: gradient.x() = -gradient.x(); break;
        case 3: gradient = -gradient; break;
        case 4: gradient.y() = -gradient.y(); break;
        case 5: gradient = Vec2f{-gradient.y(), -gradient.x()}; break;
        case 6: gradient = Vec2f{-gradient.y(), gradient.x()}; break;
        case 7: std::swap(gradient.x(), gradient.y()); break;
        case 8: gradient = Vec2f{gradient.y(), -gradient.x()}; break;
        default: KIRA_UNREACHABLE();
        }
    };
    switch (texture.orientation) {
    case 1: break;
    case 2: uv.x() = 1.0F - uv.x(); break;
    case 3: uv = Vec2f{1.0F - uv.x(), 1.0F - uv.y()}; break;
    case 4: uv.y() = 1.0F - uv.y(); break;
    case 5: uv = Vec2f{1.0F - uv.y(), 1.0F - uv.x()}; break;
    case 6: uv = Vec2f{1.0F - uv.y(), uv.x()}; break;
    case 7: std::swap(uv.x(), uv.y()); break;
    case 8: uv = Vec2f{uv.y(), 1.0F - uv.x()}; break;
    default: KIRA_UNREACHABLE();
    }
    orientGradient(duvdx);
    orientGradient(duvdy);

    auto options = texture.options;
    if (point)
        options.interpmode = OIIO::Tex::InterpMode::Closest;
    auto const sampled = texture.asset->pImpl_->sample(
        std::move(options), uv, duvdx, duvdy, texture.sampleComponentCount
    );
    return texture.apply(sampled);
}
} // namespace flux
