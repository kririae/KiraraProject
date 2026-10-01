#include "flux/Embree/EmbreeImageTexturePool.h"

#include <OpenImageIO/texture.h>

#include <utility>

#include "flux/Core/RayFootprint.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/ImageAssetPImpl.h"
#include "flux/Shading/Texture.h"

namespace flux {
struct EmbreeImageTexturePool::Entry {
    /// Keeps the OIIO texture system and handle alive.
    Ref<ImageAsset const> asset;
    OIIO::TextureSystem *textureSystem{};
    OIIO::TextureSystem::TextureHandle *textureHandle{};
    OIIO::TextureOpt options;
    std::uint8_t sampleComponentCount{};
    std::uint8_t orientation{};
    ImageComponentMapping componentMapping;
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
        options.swrap = addressMode(texture->getAddressMode());
        options.twrap = options.swrap;
        options.mipmode = OIIO::Tex::MipMode::Aniso;
        options.anisotropic = RayFootprint::maxAnisotropy;
        options.interpmode = texture->getFilterMode() == ImageTextureFilterMode::Linear
                                 ? OIIO::Tex::InterpMode::Bilinear
                                 : OIIO::Tex::InterpMode::Closest;
        // OIIO converts color components before filtering.
        options.colortransformid = asset->pImpl_->colorTransformId;

        auto componentMapping = texture->getComponentMapping();
        auto sampleComponentCount = asset->pImpl_->fileComponentCount;
        // Request RGBA for RGB images so OIIO filters the added alpha. Border
        // mode blends it with zero outside the image.
        auto const addsAlpha =
            asset->pImpl_->fileToRGBA && asset->pImpl_->fileToRGBA->a == ImageComponentSource::One;
        if (addsAlpha) {
            options.fill = 1.0F;
            sampleComponentCount = 4;
        }

        // Compose the ImageTexture mapping with the file component order.
        if (asset->pImpl_->fileToRGBA) {
            auto const &fileMapping = *asset->pImpl_->fileToRGBA;
            auto const mapSource = [&](ImageComponentSource component) {
                switch (component) {
                case ImageComponentSource::X: return fileMapping.r;
                case ImageComponentSource::Y: return fileMapping.g;
                case ImageComponentSource::Z: return fileMapping.b;
                case ImageComponentSource::W:
                    return addsAlpha ? ImageComponentSource::W : fileMapping.a;
                case ImageComponentSource::Zero:
                case ImageComponentSource::One: return component;
                }
                KIRA_UNREACHABLE();
            };
            componentMapping = {
                .r = mapSource(componentMapping.r),
                .g = mapSource(componentMapping.g),
                .b = mapSource(componentMapping.b),
                .a = mapSource(componentMapping.a),
            };
        }

        textures_[context.getIndex<ImageTexture>(texture->getContextId())] = {
            .asset = asset,
            .textureSystem = asset->pImpl_->textureSystem.get(),
            .textureHandle = asset->pImpl_->textureHandle,
            .options = options,
            .sampleComponentCount = sampleComponentCount,
            .orientation = asset->pImpl_->orientation,
            .componentMapping = componentMapping,
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
        case 5: std::swap(gradient.x(), gradient.y()); break;
        case 6: gradient = Vec2f{-gradient.y(), gradient.x()}; break;
        case 7: gradient = Vec2f{-gradient.y(), -gradient.x()}; break;
        case 8: gradient = Vec2f{gradient.y(), -gradient.x()}; break;
        default: KIRA_UNREACHABLE();
        }
    };
    switch (texture.orientation) {
    case 1: break;
    case 2: uv.x() = 1.0F - uv.x(); break;
    case 3: uv = Vec2f{1.0F - uv.x(), 1.0F - uv.y()}; break;
    case 4: uv.y() = 1.0F - uv.y(); break;
    case 5: std::swap(uv.x(), uv.y()); break;
    case 6: uv = Vec2f{1.0F - uv.y(), uv.x()}; break;
    case 7: uv = Vec2f{1.0F - uv.y(), 1.0F - uv.x()}; break;
    case 8: uv = Vec2f{uv.y(), 1.0F - uv.x()}; break;
    default: KIRA_UNREACHABLE();
    }
    orientGradient(duvdx);
    orientGradient(duvdy);

    alignas(16) auto sampled = Vec4f{};
    static_assert(sizeof(Vec4f) == 4 * sizeof(float));
    // TextureSystem may update the options during a lookup.
    auto options = texture.options;
    if (point)
        options.interpmode = OIIO::Tex::InterpMode::Closest;
    // A null threadInfo lets OIIO manage per-thread data.
    // clang-format off
    auto const found = texture.textureSystem->texture(
        /* textureHandle = */ texture.textureHandle,
        /* threadInfo =    */ nullptr,
        /* options =       */ options,
        /* s =             */ uv.x(),
        /* t =             */ uv.y(),
        /* dsdx =          */ duvdx.x(),
        /* dtdx =          */ duvdx.y(),
        /* dsdy =          */ duvdy.x(),
        /* dtdy =          */ duvdy.y(),
        /* numChannels =   */ texture.sampleComponentCount,
        /* result =        */ sampled.data()
    );
    // clang-format on
    if (!found)
        return {};

    return texture.componentMapping.apply(sampled);
}
} // namespace flux
