#include "flux/Embree/EmbreeImageTexturePool.h"

#include <OpenImageIO/imagebufalgo.h>

#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

#include "flux/Scene/ImageAssetPImpl.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto srgbRec709 = std::string_view{"srgb_rec709_scene"};
constexpr auto linearRec709 = std::string_view{"lin_rec709_scene"};
} // namespace

struct EmbreeImageTexturePool::Image {
    ImageAsset::ImageBuffer buffer;
};

struct EmbreeImageTexturePool::Entry {
    std::uint32_t imageIndex;
    OIIO::ImageBuf::WrapMode addressMode;
    ImageTextureFilterMode filterMode;
    ImageComponentMapping componentMapping;
};

EmbreeImageTexturePool::EmbreeImageTexturePool() = default;
EmbreeImageTexturePool::~EmbreeImageTexturePool() = default;

void EmbreeImageTexturePool::build(std::span<Ref<ImageTexture const> const> textures) {
    auto const addressMode = [](ImageTextureAddressMode mode) {
        switch (mode) {
        case ImageTextureAddressMode::Wrap: return OIIO::ImageBuf::WrapPeriodic;
        case ImageTextureAddressMode::Clamp: return OIIO::ImageBuf::WrapClamp;
        case ImageTextureAddressMode::Mirror: return OIIO::ImageBuf::WrapMirror;
        case ImageTextureAddressMode::Border: return OIIO::ImageBuf::WrapBlack;
        }
        KIRA_UNREACHABLE();
    };

    images_.clear();
    textures_.clear();
    images_.reserve(textures.size());
    textures_.reserve(textures.size());
    std::unordered_map<ImageAsset const *, std::uint32_t> imageIndices;
    imageIndices.reserve(textures.size());

    for (auto const &texture : textures) {
        auto const &asset = texture->getImageAsset();
        auto [iterator, inserted] =
            imageIndices.try_emplace(asset.get(), static_cast<std::uint32_t>(images_.size()));
        if (inserted) {
            if (images_.size() >= std::numeric_limits<std::uint32_t>::max())
                throw kira::Anyhow("EmbreeImageTexturePool: image count exceeds backend limits");

            auto image = asset->read();
            if (image.pendingTransform == ImageTransform::SRGB) {
                auto spec = image.storage.spec();
                spec.set_format(OIIO::TypeDesc::FLOAT);
                spec.channelformats.clear();

                auto linear = OIIO::ImageBuf{spec};
                if (!OIIO::ImageBufAlgo::colorconvert(
                        linear, image.storage, srgbRec709, linearRec709, false
                    ))
                    throw kira::Anyhow(
                        "EmbreeImageTexturePool: failed to convert image {} from sRGB to linear: "
                        "{}",
                        iterator->second, linear.geterror()
                    );
                image.storage = std::move(linear);
                image.componentType = ImageComponentType::Float32;
                image.pendingTransform = ImageTransform::Identity;
            }

            images_.push_back({.buffer = std::move(image)});
        }

        textures_.push_back({
            .imageIndex = iterator->second,
            .addressMode = addressMode(texture->getAddressMode()),
            .filterMode = texture->getFilterMode(),
            .componentMapping = texture->getComponentMapping(),
        });
    }
}

void EmbreeImageTexturePool::clear() noexcept {
    textures_.clear();
    images_.clear();
}

Vec4f EmbreeImageTexturePool::Impl::eval4f(std::uint32_t index, Vec2f uv) const noexcept {
    auto const &texture = textures[index];
    auto const &image = images[texture.imageIndex].buffer;
    alignas(16) auto sampled = Vec4f{};
    static_assert(sizeof(Vec4f) == 4 * sizeof(float));
    auto components = OIIO::span<float>{sampled.data(), image.componentCount};
    if (texture.filterMode == ImageTextureFilterMode::Linear) {
        image.storage.interppixel_NDC(uv.x(), uv.y(), components, texture.addressMode);
    } else {
        auto const x = static_cast<int>(std::floor(uv.x() * static_cast<float>(image.extent.x())));
        auto const y = static_cast<int>(std::floor(uv.y() * static_cast<float>(image.extent.y())));
        image.storage.getpixel(x, y, 0, components, texture.addressMode);
    }

    return texture.componentMapping.apply(sampled);
}
} // namespace flux
