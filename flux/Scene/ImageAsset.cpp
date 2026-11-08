#include "flux/Scene/ImageAsset.h"

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/Scene/ImageAssetPImpl.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
/// ID of the next ImageAsset. Assets are created concurrently, so it is atomic.
std::atomic_size_t nextImageAssetId{0};

constexpr auto srgbRec709 = "srgb_rec709_scene";
constexpr auto linearRec709Scene = "lin_rec709_scene";

/// Raised for a file that is not a well-formed image. The message is the reason.
struct BadImage final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Name of the 1x1 magenta image that every texture system holds in place of a bad asset.
constexpr auto magentaName = "flux-magenta.tif";

[[nodiscard]] std::vector<unsigned char> encodeMagenta() {
    auto bytes = std::vector<unsigned char>{};
    auto proxy = OIIO::Filesystem::IOVecOutput{bytes};
    auto output = OIIO::ImageOutput::create(magentaName, &proxy);
    auto const spec = OIIO::ImageSpec{1, 1, 3, OIIO::TypeDesc::UINT8};
    auto const pixel = std::array<std::uint8_t, 3>{255, 0, 255};
    if (!output || !output->open(magentaName, spec) ||
        !output->write_image(OIIO::TypeDesc::UINT8, pixel.data()) || !output->close())
        throw kira::Anyhow("ImageAssetPool: failed to encode the magenta image");
    return bytes;
}

/// The magenta image in memory. \c reader serves \c bytes as a file. Magenta is the same in sRGB
/// and linear.
struct Magenta {
    std::vector<unsigned char> bytes{encodeMagenta()};
    OIIO::Filesystem::IOMemReader reader{bytes.data(), bytes.size()};
};

/// A texture system and the magenta image that its cache may read at any time. The system is
/// destroyed first, so the cache never outlives the image.
struct MagentaTextureSystem {
    Magenta magenta;
    std::shared_ptr<OIIO::TextureSystem> system;
};

void validateImageSpec(OIIO::ImageSpec const &spec) {
    if (spec.width <= 0 || spec.height <= 0 || spec.depth != 1 || spec.deep)
        throw BadImage("it must be non-empty, flat, and 2D");
    if (spec.nchannels < 1 || spec.nchannels > 4)
        throw BadImage("it must have one to four channels");
    if (spec.x != 0 || spec.y != 0 || spec.z != 0 || spec.full_x != 0 || spec.full_y != 0 ||
        spec.full_z != 0 || spec.full_width != spec.width || spec.full_height != spec.height ||
        spec.full_depth != spec.depth)
        throw BadImage("its data and display windows must match and start at the origin");
    auto const orientation = spec.get_int_attribute("Orientation", 1);
    if (orientation < 1 || orientation > 8)
        throw BadImage("its orientation must be valid");
}

[[nodiscard]] ImageComponentType chooseComponentType(OIIO::ImageSpec const &spec) noexcept {
    auto result = ImageComponentType::UNorm8;
    for (auto component = 0; component < spec.nchannels; ++component) {
        auto const format = spec.channelformat(component);
        if (format == OIIO::TypeDesc::UINT8)
            continue;
        if (format == OIIO::TypeDesc::HALF) {
            result = ImageComponentType::Float16;
            continue;
        }
        return ImageComponentType::Float32;
    }
    return result;
}

[[nodiscard]] std::filesystem::path canonicalize(std::filesystem::path const &path) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

[[nodiscard]] ImageComponentMapping makeDefaultComponentMapping(std::uint8_t componentCount) {
    using Source = ImageComponentSource;
    switch (componentCount) {
    case 1: return {.r = Source::X, .g = Source::X, .b = Source::X, .a = Source::One};
    case 2: return {.r = Source::X, .g = Source::X, .b = Source::X, .a = Source::Y};
    default: return {};
    }
}

[[nodiscard]] OIIO::ImageBuf appendAlpha(OIIO::ImageBuf const &source, std::string_view filename) {
    auto result = OIIO::ImageBuf{};
    if (!OIIO::ImageBufAlgo::channels(
            result, source, 4, std::array{0, 1, 2, -1}, std::array{0.0F, 0.0F, 0.0F, 1.0F}
        ))
        throw kira::Anyhow(
            "ImageAsset: failed to append alpha to '{}': {}", filename, result.geterror()
        );
    return result;
}
} // namespace

ImageAssetPool::pImpl::pImpl() {
    auto createTextureSystem = [](ImageColorSpace colorSpace) {
        // Created before the image cache, so that the cache is destroyed first on every path.
        auto const owner = std::make_shared<MagentaTextureSystem>();
        auto imageCache = OIIO::ImageCache::create(/* shared = */ false);
        if (!imageCache)
            throw kira::Anyhow("ImageAssetPool: failed to create an OpenImageIO image cache");
        if (!imageCache->attribute("autotile", 64))
            throw kira::Anyhow("ImageAssetPool: failed to configure the OpenImageIO tile cache");
        if (!imageCache->attribute("automip", 1))
            throw kira::Anyhow("ImageAssetPool: failed to enable OpenImageIO mip generation");
        if (!imageCache->attribute("colorspace", linearRec709Scene))
            throw kira::Anyhow("ImageAssetPool: failed to configure the working color space");
        if (!imageCache->attribute("unassociatedalpha", 1))
            throw kira::Anyhow("ImageAssetPool: failed to configure straight alpha");
        if (colorSpace == ImageColorSpace::SRGB && !imageCache->attribute("forcefloat", 1))
            throw kira::Anyhow(
                "ImageAssetPool: failed to configure the sRGB image cache component type"
            );

        OIIO::Filesystem::IOProxy *proxy = &owner->magenta.reader;
        auto config = OIIO::ImageSpec{};
        config.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &proxy);
        if (!imageCache->add_file(OIIO::ustring{magentaName}, nullptr, &config))
            throw kira::Anyhow("ImageAssetPool: failed to add the magenta image to the cache");

        owner->system = OIIO::TextureSystem::create(/* shared = */ false, std::move(imageCache));
        if (!owner->system)
            throw kira::Anyhow("ImageAssetPool: failed to create an OpenImageIO texture system");
        if (!owner->system->attribute("flip_t", 1))
            throw kira::Anyhow("ImageAssetPool: failed to configure texture coordinates");

        return std::shared_ptr<OIIO::TextureSystem>{owner, owner->system.get()};
    };

    linearTextureSystem = createTextureSystem(ImageColorSpace::Linear);
    srgbTextureSystem = createTextureSystem(ImageColorSpace::SRGB);
}

ImageAsset::pImpl::pImpl(
    std::shared_ptr<OIIO::TextureSystem> system, std::filesystem::path const &path,
    ImageColorSpace colorSpace
)
    : textureSystem(std::move(system)), filename(path.string()), fileColorSpace(colorSpace) {
    auto const imageCache = textureSystem->imagecache();
    if (!imageCache->add_file(filename))
        throw BadImage(imageCache->geterror());

    auto *imageHandle = imageCache->get_image_handle(filename);
    if (!imageHandle || !imageCache->good(imageHandle))
        throw BadImage(imageCache->geterror());

    auto spec = OIIO::ImageSpec{};
    if (!imageCache->get_imagespec(
            imageHandle, /* threadInfo = */ nullptr, spec, /* subimage = */ 0
        ))
        throw BadImage(imageCache->geterror());
    validateImageSpec(spec);

    if (colorSpace == ImageColorSpace::SRGB && spec.nchannels < 3)
        throw BadImage("an sRGB image must have RGB or RGBA channels");

    auto width = spec.width;
    auto height = spec.height;
    orientation = static_cast<std::uint8_t>(spec.get_int_attribute("Orientation", 1));
    if (transposesAxes(orientation))
        std::swap(width, height);
    extent = Vec2u{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    fileComponentCount = static_cast<std::uint8_t>(spec.nchannels);
    componentType = chooseComponentType(spec);
    componentCount = fileComponentCount == 3 ? 4 : fileComponentCount;
    defaultComponentMapping = makeDefaultComponentMapping(componentCount);

    if (colorSpace == ImageColorSpace::SRGB) {
        colorTransformId = textureSystem->get_colortransform_id(
            OIIO::ustring{srgbRec709}, OIIO::ustring{linearRec709Scene}
        );
        if (colorTransformId < 0)
            throw kira::Anyhow("ImageAsset: failed to create the sRGB color transform");
    }

    auto options = OIIO::TextureOpt{};
    options.colortransformid = colorTransformId;
    textureHandle =
        textureSystem->get_texture_handle(filename, /* threadInfo = */ nullptr, &options);
    if (!textureHandle || !textureSystem->good(textureHandle))
        throw kira::Anyhow("ImageAsset: failed to create a texture handle for '{}'", path.string());
}

ImageAsset::ImageAsset(std::unique_ptr<pImpl> pImpl, bool placeholder) noexcept
    : pImpl_(std::move(pImpl)), id_(nextImageAssetId.fetch_add(1, std::memory_order_relaxed)),
      placeholder_(placeholder) {}
ImageAsset::~ImageAsset() = default;

Vec2u ImageAsset::getExtent() const noexcept { return pImpl_->extent; }
std::uint8_t ImageAsset::getComponentCount() const noexcept { return pImpl_->componentCount; }
ImageComponentType ImageAsset::getComponentType() const noexcept { return pImpl_->componentType; }
ImageComponentMapping ImageAsset::getDefaultComponentMapping() const noexcept {
    return pImpl_->defaultComponentMapping;
}

std::vector<ImageAsset::ImageBuffer> ImageAsset::readMipChain() const {
    auto const encodeSRGB = pImpl_->fileColorSpace == ImageColorSpace::SRGB &&
                            pImpl_->componentType == ImageComponentType::UNorm8;
    auto const format = [this, encodeSRGB] {
        if (encodeSRGB)
            return OIIO::TypeDesc::FLOAT;

        switch (pImpl_->componentType) {
        case ImageComponentType::UNorm8: return OIIO::TypeDesc::UINT8;
        case ImageComponentType::Float16: return OIIO::TypeDesc::HALF;
        case ImageComponentType::Float32: return OIIO::TypeDesc::FLOAT;
        }
        KIRA_UNREACHABLE();
    }();
    auto result = std::vector<ImageBuffer>{};
    auto width = pImpl_->extent.x();
    auto height = pImpl_->extent.y();
    if (transposesAxes(pImpl_->orientation))
        std::swap(width, height);
    auto options = OIIO::TextureOpt{};
    options.colortransformid = pImpl_->colorTransformId;

    for (auto level = 0;; ++level) {
        auto spec = OIIO::ImageSpec{
            static_cast<int>(width), static_cast<int>(height), pImpl_->fileComponentCount, format
        };
        spec.attribute("Orientation", pImpl_->orientation);
        auto source = OIIO::ImageBuf{spec};
        auto const roi =
            OIIO::ROI{0, spec.width, 0, spec.height, 0, 1, 0, pImpl_->fileComponentCount};
        if (!pImpl_->textureSystem->get_texels(
                pImpl_->textureHandle, /* threadInfo = */ nullptr, options, level, roi, format,
                source.localpixels_as_writable_byte_image_span()
            ))
            throw kira::Anyhow(
                "ImageAsset: failed to read mip level {} from '{}': {}", level,
                pImpl_->filename.string(), pImpl_->textureSystem->geterror()
            );

        if (pImpl_->fileComponentCount == 3)
            source = appendAlpha(source, pImpl_->filename.string());

        if (source.orientation() != 1) {
            auto oriented = OIIO::ImageBuf{};
            if (!OIIO::ImageBufAlgo::reorient(oriented, source))
                throw kira::Anyhow(
                    "ImageAsset: failed to orient '{}': {}", pImpl_->filename.string(),
                    oriented.geterror()
                );
            source = std::move(oriented);
        }

        auto image = OIIO::ImageBuf{};
        if (!OIIO::ImageBufAlgo::flip(image, source))
            throw kira::Anyhow(
                "ImageAsset: failed to flip '{}': {}", pImpl_->filename.string(), image.geterror()
            );

        if (encodeSRGB) {
            auto encodedSpec = image.spec();
            encodedSpec.format = OIIO::TypeDesc::UINT8;
            auto encoded = OIIO::ImageBuf{encodedSpec};
            if (!OIIO::ImageBufAlgo::colorconvert(
                    encoded, image, linearRec709Scene, srgbRec709, /* unpremult = */ false
                ))
                throw kira::Anyhow(
                    "ImageAsset: failed to encode mip level {} for '{}': {}", level,
                    pImpl_->filename.string(), encoded.geterror()
                );
            image = std::move(encoded);
        }

        auto const extent = Vec2u{
            static_cast<std::uint32_t>(image.spec().width),
            static_cast<std::uint32_t>(image.spec().height),
        };
        result.push_back({
            .image = std::move(image),
            .extent = extent,
            .componentCount = pImpl_->componentCount,
            .componentType = pImpl_->componentType,
            .colorSpace = encodeSRGB ? ImageColorSpace::SRGB : ImageColorSpace::Linear,
        });
        if (width == 1 && height == 1)
            break;
        width = std::max(width / 2, 1U);
        height = std::max(height / 2, 1U);
    }
    return result;
}

ImageAssetPool::ImageAssetPool() : pImpl_(std::make_unique<pImpl>()) {}
ImageAssetPool::~ImageAssetPool() = default;

Ref<ImageAsset const> ImageAssetPool::getOrCreate(ImageAssetSpec const &spec) {
    auto canonical = spec;
    if (!spec.path.empty())
        canonical.path = canonicalize(spec.path);
    if (canonical.colorSpace.empty())
        canonical.colorSpace = linearRec709;
    return assets_.acquire(canonical, [&] {
        auto const colorSpace =
            canonical.colorSpace == srgbTexture ? ImageColorSpace::SRGB : ImageColorSpace::Linear;
        auto const &textureSystem = colorSpace == ImageColorSpace::SRGB
                                        ? pImpl_->srgbTextureSystem
                                        : pImpl_->linearTextureSystem;
        auto const create = [&](std::filesystem::path const &path, bool placeholder) {
            return Ref<ImageAsset const>{new ImageAsset(
                std::make_unique<ImageAsset::pImpl>(textureSystem, path, colorSpace), placeholder
            )};
        };
        if (canonical.path.empty())
            return create(magentaName, true);
        try {
            return create(canonical.path, false);
        } catch (BadImage const &bad) {
            LogWarn(
                "ImageAsset: '{}' is not a well-formed image ({}), using a magenta pixel",
                canonical.path.string(), bad.what()
            );
        }
        return create(magentaName, true);
    });
}

std::string ImageAssetPool::chooseColorSpace(std::filesystem::path const &path) const {
    auto const imageCache = pImpl_->linearTextureSystem->imagecache();
    auto const *spec = imageCache->imagespec(OIIO::ustring{canonicalize(path).string()});
    if (!spec) {
        (void)imageCache->geterror();
        return {};
    }

    auto const isUNorm8 = chooseComponentType(*spec) == ImageComponentType::UNorm8;
    return isUNorm8 && spec->nchannels >= 3 ? std::string{srgbTexture} : std::string{};
}

void ImageAssetPool::collectGarbage() {
    // A count of one means the pool holds the only reference: no texture and no runtime uses it.
    assets_.prune([](Ref<ImageAsset const> const &asset) { return asset.getRefCount() > 1; });
}
} // namespace flux
