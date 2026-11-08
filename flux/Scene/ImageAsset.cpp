#include "flux/Scene/ImageAsset.h"

#include <OpenImageIO/color.h>
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

/// \pre \p name is a known color space.
[[nodiscard]] bool isLinearRec709(std::string_view name) {
    return OIIO::ColorConfig::default_colorconfig().equivalent(name, linearRec709);
}

/// The color space that the OCIO file rules give \p path, or empty if no rule but the default one
/// matches. The default rule matches every file, so it says nothing about this one.
[[nodiscard]] OIIO::string_view findColorSpaceByFileRules(std::filesystem::path const &path) {
    auto const &config = OIIO::ColorConfig::default_colorconfig();
    auto const filename = path.string();
    if (config.filepathOnlyMatchesDefaultRule(filename))
        return {};
    return config.getColorSpaceFromFilepath(filename);
}

/// The name that the OCIO config gives the color space of \p spec, so that every alias shares an
/// asset. An empty name, or a name that the config does not know, is lin_rec709.
[[nodiscard]] std::string canonicalizeColorSpace(ImageAssetSpec const &spec) {
    auto const &config = OIIO::ColorConfig::default_colorconfig();
    auto name = OIIO::string_view{spec.colorSpace};
    if (name.empty() && !spec.path.empty())
        name = findColorSpaceByFileRules(spec.path);
    if (name.empty())
        name = linearRec709;

    // `resolve` returns a name that it does not know unchanged.
    if (config.getColorSpaceIndex(name) < 0) {
        LogWarn(
            "ImageAsset: '{}' has the unknown color space '{}', which becomes {}",
            spec.path.string(), name, linearRec709
        );
        name = linearRec709;
    }
    return std::string{config.resolve(name)};
}

[[nodiscard]] std::filesystem::path canonicalize(std::filesystem::path const &path) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

[[nodiscard]] OIIO::TypeDesc toTypeDesc(ImageComponentType type) noexcept {
    switch (type) {
    case ImageComponentType::UNorm8: return OIIO::TypeDesc::UINT8;
    case ImageComponentType::Float16: return OIIO::TypeDesc::HALF;
    case ImageComponentType::Float32: return OIIO::TypeDesc::FLOAT;
    }
    KIRA_UNREACHABLE();
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

/// The number of mip levels that the file holds, which is 1 for a file without mip levels.
[[nodiscard]] int countMipLevels(std::string const &filename) {
    auto const input = OIIO::ImageInput::open(filename);
    if (!input)
        throw kira::Anyhow("ImageAsset: failed to open '{}': {}", filename, OIIO::geterror());
    auto count = 1;
    while (input->seek_subimage(0, count))
        ++count;
    return count;
}
} // namespace

ImageAssetPool::pImpl::pImpl() {
    auto createTextureSystem = [](bool converts) {
        // Created before the image cache, so that the cache is destroyed first on every path.
        auto const owner = std::make_shared<MagentaTextureSystem>();
        auto imageCache = OIIO::ImageCache::create(/* shared = */ false);
        if (!imageCache)
            throw kira::Anyhow("ImageAssetPool: failed to create an OpenImageIO image cache");
        if (!imageCache->attribute("autotile", 64))
            throw kira::Anyhow("ImageAssetPool: failed to configure the OpenImageIO tile cache");
        if (!imageCache->attribute("automip", 1))
            throw kira::Anyhow("ImageAssetPool: failed to enable OpenImageIO mip generation");
        if (!imageCache->attribute("colorspace", linearRec709))
            throw kira::Anyhow("ImageAssetPool: failed to configure the working color space");
        if (!imageCache->attribute("unassociatedalpha", 1))
            throw kira::Anyhow("ImageAssetPool: failed to configure straight alpha");
        if (converts && !imageCache->attribute("forcefloat", 1))
            throw kira::Anyhow(
                "ImageAssetPool: failed to configure the converting image cache component type"
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

    linearTextureSystem = createTextureSystem(/* converts = */ false);
    convertingTextureSystem = createTextureSystem(/* converts = */ true);
}

ImageAsset::pImpl::pImpl(
    std::shared_ptr<OIIO::TextureSystem> system, std::filesystem::path const &path,
    std::string_view colorSpace
)
    : textureSystem(std::move(system)), filename(path.string()), colorSpace{colorSpace} {
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

    auto const converts = !isLinearRec709(colorSpace);
    if (converts && spec.nchannels < 3)
        throw BadImage("an image of another color space must have RGB or RGBA channels");

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

    if (converts) {
        colorTransformId = textureSystem->get_colortransform_id(
            OIIO::ustring{colorSpace}, OIIO::ustring{linearRec709}
        );
        if (colorTransformId < 0)
            throw kira::Anyhow(
                "ImageAsset: failed to create the color transform from '{}'", colorSpace
            );
        // Only an 8-bit sRGB image stays 8-bit. Any other conversion leaves its range.
        if (componentType == ImageComponentType::UNorm8) {
            if (OIIO::ColorConfig::default_colorconfig().equivalent(colorSpace, srgbTexture))
                storedColorSpace = ImageColorSpace::SRGB;
            else
                componentType = ImageComponentType::Float16;
        }
    }

    auto options = OIIO::TextureOpt{};
    options.colortransformid = colorTransformId;
    textureHandle =
        textureSystem->get_texture_handle(filename, /* threadInfo = */ nullptr, &options);
    if (!textureHandle || !textureSystem->good(textureHandle))
        throw kira::Anyhow("ImageAsset: failed to create a texture handle for '{}'", path.string());
}

OIIO::ImageBuf ImageAsset::pImpl::readLevel(int miplevel) const {
    if (filename == OIIO::ustring{magentaName}) {
        // The placeholder is not a file, so only the texture system can read it.
        auto const format = toTypeDesc(componentType);
        auto const width = static_cast<int>(extent.x());
        auto const height = static_cast<int>(extent.y());
        auto texels = OIIO::ImageBuf{OIIO::ImageSpec{width, height, fileComponentCount, format}};
        auto const roi = OIIO::ROI{0, width, 0, height, 0, 1, 0, fileComponentCount};
        auto options = OIIO::TextureOpt{};
        if (!textureSystem->get_texels(
                textureHandle, /* threadInfo = */ nullptr, options, /* miplevel = */ 0, roi, format,
                texels.localpixels_as_writable_byte_image_span()
            ))
            throw kira::Anyhow(
                "ImageAsset: failed to read the placeholder: {}", textureSystem->geterror()
            );
        return texels;
    }

    // Alpha is straight.
    auto config = OIIO::ImageSpec{};
    config.attribute("oiio:UnassociatedAlpha", 1);
    auto image = OIIO::ImageBuf{
        filename.string(), /* subimage = */ 0, miplevel, /* imagecache = */ nullptr, &config
    };
    if (!image.read(
            /* subimage = */ 0, miplevel, /* chbegin = */ 0, fileComponentCount,
            /* force = */ true, toTypeDesc(componentType)
        ))
        throw kira::Anyhow(
            "ImageAsset: failed to read '{}': {}", filename.string(), image.geterror()
        );
    if (colorTransformId == 0 || storedColorSpace == ImageColorSpace::SRGB)
        return image;

    auto converted = OIIO::ImageBuf{};
    if (!OIIO::ImageBufAlgo::colorconvert(
            converted, image, colorSpace, linearRec709, /* unpremult = */ false
        ))
        throw kira::Anyhow(
            "ImageAsset: failed to convert '{}' from '{}': {}", filename.string(), colorSpace,
            converted.geterror()
        );
    return converted;
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
    auto const encodeSRGB = pImpl_->storedColorSpace == ImageColorSpace::SRGB;
    auto const fileName = pImpl_->filename.string();

    // A level of the file as a bottom-up level of the result, with an alpha channel.
    auto const prepare = [&](OIIO::ImageBuf image) {
        if (image.orientation() != 1) {
            auto oriented = OIIO::ImageBuf{};
            if (!OIIO::ImageBufAlgo::reorient(oriented, image))
                throw kira::Anyhow(
                    "ImageAsset: failed to orient '{}': {}", fileName, oriented.geterror()
                );
            image = std::move(oriented);
        }

        auto flipped = OIIO::ImageBuf{};
        if (!OIIO::ImageBufAlgo::flip(flipped, image))
            throw kira::Anyhow("ImageAsset: failed to flip '{}': {}", fileName, flipped.geterror());
        return pImpl_->fileComponentCount == 3 ? appendAlpha(flipped, fileName) : flipped;
    };

    auto const makeBuffer = [this, encodeSRGB](OIIO::ImageBuf image) {
        auto const extent = Vec2u{
            static_cast<std::uint32_t>(image.spec().width),
            static_cast<std::uint32_t>(image.spec().height),
        };
        return ImageBuffer{
            .image = std::move(image),
            .extent = extent,
            .componentCount = pImpl_->componentCount,
            .componentType = pImpl_->componentType,
            .colorSpace = encodeSRGB ? ImageColorSpace::SRGB : ImageColorSpace::Linear,
        };
    };

    auto result = std::vector<ImageBuffer>{};
    auto flipped = prepare(pImpl_->readLevel(0));

    // A file with mip levels keeps them, as the image cache does.
    auto const authoredLevelCount =
        pImpl_->filename == OIIO::ustring{magentaName} ? 1 : countMipLevels(fileName);
    if (authoredLevelCount > 1) {
        result.push_back(makeBuffer(std::move(flipped)));
        for (auto level = 1; level < authoredLevelCount; ++level)
            result.push_back(makeBuffer(prepare(pImpl_->readLevel(level))));
        return result;
    }

    // An sRGB image filters in linear space, so it keeps a linear copy of the level above.
    auto linear = OIIO::ImageBuf{};
    if (encodeSRGB) {
        auto linearSpec = flipped.spec();
        linearSpec.format = OIIO::TypeDesc::FLOAT;
        linear = OIIO::ImageBuf{linearSpec};
        if (!OIIO::ImageBufAlgo::colorconvert(
                linear, flipped, srgbTexture, linearRec709, /* unpremult = */ false
            ))
            throw kira::Anyhow(
                "ImageAsset: failed to decode '{}': {}", fileName, linear.geterror()
            );
    }

    result.push_back(makeBuffer(std::move(flipped)));
    auto const channelCount = result.front().image.nchannels();
    auto const format = encodeSRGB ? OIIO::TypeDesc::FLOAT : toTypeDesc(pImpl_->componentType);
    auto width = result.front().extent.x();
    auto height = result.front().extent.y();
    while (width > 1 || height > 1) {
        width = std::max(width / 2, 1U);
        height = std::max(height / 2, 1U);

        auto const &above = encodeSRGB ? linear : result.back().image;
        auto level = OIIO::ImageBuf{
            OIIO::ImageSpec{static_cast<int>(width), static_cast<int>(height), channelCount, format}
        };
        if (!OIIO::ImageBufAlgo::resample(level, above, /* interpolate = */ true))
            throw kira::Anyhow(
                "ImageAsset: failed to make a mip level of '{}': {}", fileName, level.geterror()
            );

        if (!encodeSRGB) {
            result.push_back(makeBuffer(std::move(level)));
            continue;
        }

        auto encodedSpec = level.spec();
        encodedSpec.format = OIIO::TypeDesc::UINT8;
        auto encoded = OIIO::ImageBuf{encodedSpec};
        if (!OIIO::ImageBufAlgo::colorconvert(
                encoded, level, linearRec709, srgbTexture, /* unpremult = */ false
            ))
            throw kira::Anyhow(
                "ImageAsset: failed to encode a mip level of '{}': {}", fileName, encoded.geterror()
            );
        linear = std::move(level);
        result.push_back(makeBuffer(std::move(encoded)));
    }
    return result;
}

ImageAssetPool::ImageAssetPool() : pImpl_(std::make_unique<pImpl>()) {}
ImageAssetPool::~ImageAssetPool() = default;

Ref<ImageAsset const> ImageAssetPool::getOrCreate(ImageAssetSpec const &spec) {
    auto canonical = spec;
    if (!spec.path.empty())
        canonical.path = canonicalize(spec.path);
    canonical.colorSpace = canonicalizeColorSpace(spec);
    return assets_.acquire(canonical, [&] {
        auto const create = [&](std::filesystem::path const &path, bool placeholder,
                                std::string_view colorSpace) {
            auto const &textureSystem = isLinearRec709(colorSpace)
                                            ? pImpl_->linearTextureSystem
                                            : pImpl_->convertingTextureSystem;
            return Ref<ImageAsset const>{new ImageAsset(
                std::make_unique<ImageAsset::pImpl>(textureSystem, path, colorSpace), placeholder
            )};
        };
        if (canonical.path.empty())
            return create(magentaName, true, linearRec709);

        try {
            return create(canonical.path, false, canonical.colorSpace);
        } catch (BadImage const &bad) {
            LogWarn(
                "ImageAsset: '{}' is not a well-formed image ({}), using a magenta pixel",
                canonical.path.string(), bad.what()
            );
        }
        return create(magentaName, true, linearRec709);
    });
}

std::string ImageAssetPool::chooseColorSpace(std::filesystem::path const &path) const {
    auto const imageCache = pImpl_->linearTextureSystem->imagecache();
    auto const *spec = imageCache->imagespec(OIIO::ustring{canonicalize(path).string()});
    if (!spec) {
        (void)imageCache->geterror();
        return std::string{linearRec709};
    }

    auto const isUNorm8 = chooseComponentType(*spec) == ImageComponentType::UNorm8;
    return std::string{isUNorm8 && spec->nchannels >= 3 ? srgbTexture : linearRec709};
}

void ImageAssetPool::collectGarbage() {
    // A count of one means the pool holds the only reference: no texture and no runtime uses it.
    assets_.prune([](Ref<ImageAsset const> const &asset) { return asset.getRefCount() > 1; });
}
} // namespace flux
