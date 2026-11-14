#include "flux/Scene/ImageAsset.h"

#include <OpenImageIO/color.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>
#include <fmt/format.h>

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

Vec4f const unusableTexel{};

constexpr auto emptyExtent = Vec2u{1, 1};

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

[[nodiscard]] std::string describeReadFailure(int miplevel, std::string const &reason) {
    return reason.empty() ? fmt::format("mip level {} failed to read", miplevel)
                          : fmt::format("mip level {}: {}", miplevel, reason);
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
[[nodiscard]] int countAuthoredMipLevels(std::string const &filename) {
    auto const input = OIIO::ImageInput::open(filename);
    if (!input)
        throw BadImage(OIIO::geterror());
    auto count = 1;
    while (input->seek_subimage(0, count))
        ++count;
    return count;
}
} // namespace

ImageAssetPool::pImpl::pImpl() {
    auto createTextureSystem = [](bool converts) {
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

        auto system = OIIO::TextureSystem::create(/* shared = */ false, std::move(imageCache));
        if (!system)
            throw kira::Anyhow("ImageAssetPool: failed to create an OpenImageIO texture system");
        if (!system->attribute("flip_t", 1))
            throw kira::Anyhow("ImageAssetPool: failed to configure texture coordinates");

        return system;
    };

    linearTextureSystem = createTextureSystem(/* converts = */ false);
    convertingTextureSystem = createTextureSystem(/* converts = */ true);
}

ImageAsset::pImpl::pImpl()
    : extent{emptyExtent}, componentCount{4}, componentType{ImageComponentType::UNorm8},
      defaultComponentMapping{makeDefaultComponentMapping(4)}, orientation{1},
      fileComponentCount{4}, authoredLevelCount{1} {}

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

    authoredLevelCount = countAuthoredMipLevels(filename.string());

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
        auto const &colorConfig = OIIO::ColorConfig::default_colorconfig();
        if (!colorConfig.createColorProcessor(
                colorConfig.resolve(colorSpace), colorConfig.resolve(linearRec709)
            ))
            throw kira::Anyhow(
                "ImageAsset: failed to create the color processor from '{}'", colorSpace
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

Vec4f ImageAsset::pImpl::sample(
    OIIO::TextureOpt options, Vec2f uv, Vec2f duvdx, Vec2f duvdy, int channelCount
) const {
    if (!textureHandle)
        return unusableTexel;

    options.missingcolor = unusableTexel.data();
    alignas(16) auto sampled = Vec4f{};
    static_assert(sizeof(Vec4f) == 4 * sizeof(float));
    // clang-format off
    auto const found = textureSystem->texture(
        /* textureHandle = */ textureHandle,
        /* threadInfo =    */ nullptr,
        /* options =       */ options,
        /* s =             */ uv.x(),
        /* t =             */ uv.y(),
        /* dsdx =          */ duvdx.x(),
        /* dtdx =          */ duvdx.y(),
        /* dsdy =          */ duvdy.x(),
        /* dtdy =          */ duvdy.y(),
        /* numChannels =   */ channelCount,
        /* result =        */ sampled.data()
    );
    // clang-format on
    if (found)
        return sampled;

    LogWarn("ImageAsset: failed to sample '{}': {}", filename.string(), textureSystem->geterror());
    return unusableTexel;
}

OIIO::ImageBuf ImageAsset::pImpl::readLevel(int miplevel) const {
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
        throw BadImage(describeReadFailure(miplevel, image.geterror()));
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

ImageAsset::ImageAsset(std::unique_ptr<pImpl> pImpl) noexcept
    : pImpl_(std::move(pImpl)), id_(nextImageAssetId.fetch_add(1, std::memory_order_relaxed)) {}
ImageAsset::~ImageAsset() = default;

Vec2u ImageAsset::getExtent() const noexcept { return pImpl_->extent; }
std::uint8_t ImageAsset::getComponentCount() const noexcept { return pImpl_->componentCount; }
ImageComponentType ImageAsset::getComponentType() const noexcept { return pImpl_->componentType; }
ImageComponentMapping ImageAsset::getDefaultComponentMapping() const noexcept {
    return pImpl_->defaultComponentMapping;
}

ImageAsset::ImageBuffer ImageAsset::pImpl::makeBuffer(OIIO::ImageBuf image) const {
    auto const extent = Vec2u{
        static_cast<std::uint32_t>(image.spec().width),
        static_cast<std::uint32_t>(image.spec().height),
    };
    return ImageBuffer{
        .image = std::move(image),
        .extent = extent,
        .componentCount = componentCount,
        .componentType = componentType,
        .colorSpace = storedColorSpace,
    };
}

std::vector<ImageAsset::ImageBuffer> ImageAsset::readMipChain() const {
    if (!pImpl_->filename.empty()) {
        try {
            return readFileMipChain();
        } catch (BadImage const &bad) {
            LogWarn("ImageAsset: '{}' cannot be read ({})", pImpl_->filename.string(), bad.what());
        }
    }

    auto result = std::vector<ImageBuffer>{};
    auto const spec = OIIO::ImageSpec{
        static_cast<int>(emptyExtent.x()), static_cast<int>(emptyExtent.y()),
        pImpl_->componentCount, toTypeDesc(pImpl_->componentType)
    };
    auto texel = OIIO::ImageBuf{spec};
    if (!OIIO::ImageBufAlgo::fill(
            texel, OIIO::cspan<float>{unusableTexel.data(), pImpl_->componentCount}
        ))
        throw kira::Anyhow(
            "ImageAsset: failed to fill the texel of '{}'", pImpl_->filename.string()
        );
    result.push_back(pImpl_->makeBuffer(std::move(texel)));
    return result;
}

std::vector<ImageAsset::ImageBuffer> ImageAsset::readFileMipChain() const {
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

    auto result = std::vector<ImageBuffer>{};
    auto flipped = prepare(pImpl_->readLevel(0));

    // A file with mip levels keeps them, as the image cache does.
    auto const authoredLevelCount = pImpl_->authoredLevelCount;
    if (authoredLevelCount > 1) {
        result.push_back(pImpl_->makeBuffer(std::move(flipped)));
        for (auto level = 1; level < authoredLevelCount; ++level)
            result.push_back(pImpl_->makeBuffer(prepare(pImpl_->readLevel(level))));
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

    result.push_back(pImpl_->makeBuffer(std::move(flipped)));
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
            result.push_back(pImpl_->makeBuffer(std::move(level)));
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
        result.push_back(pImpl_->makeBuffer(std::move(encoded)));
    }
    return result;
}

ImageAssetPool::ImageAssetPool() : pImpl_(std::make_unique<pImpl>()) {}
ImageAssetPool::~ImageAssetPool() = default;

Ref<ImageAsset const> ImageAssetPool::getOrCreate(ImageAssetSpec const &spec) {
    auto canonical = spec;
    if (!spec.path.empty())
        canonical.path = canonicalize(spec.path);
    auto resolved = spec;
    if (spec.colorSpace == autoColorSpace)
        resolved.colorSpace = chooseColorSpace(spec.path);
    canonical.colorSpace = canonicalizeColorSpace(resolved);
    return assets_.acquire(canonical, [&] {
        if (canonical.path.empty())
            return Ref<ImageAsset const>{new ImageAsset(std::make_unique<ImageAsset::pImpl>())};

        auto const &textureSystem = isLinearRec709(canonical.colorSpace)
                                        ? pImpl_->linearTextureSystem
                                        : pImpl_->convertingTextureSystem;
        try {
            return Ref<ImageAsset const>{new ImageAsset(
                std::make_unique<ImageAsset::pImpl>(
                    textureSystem, canonical.path, canonical.colorSpace
                )
            )};
        } catch (BadImage const &bad) {
            LogWarn(
                "ImageAsset: '{}' is not a well-formed image ({})", canonical.path.string(),
                bad.what()
            );
        }
        return Ref<ImageAsset const>{new ImageAsset(std::make_unique<ImageAsset::pImpl>())};
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
