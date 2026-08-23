#include "flux/Scene/ImageAsset.h"

#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "flux/Scene/ImageAssetPImpl.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto srgbRec709 = "srgb_rec709_scene";
constexpr auto linearRec709 = "lin_rec709_scene";

void validateImageSpec(OIIO::ImageSpec const &spec, std::string_view filename) {
    if (spec.width <= 0 || spec.height <= 0 || spec.depth != 1 || spec.deep)
        throw kira::Anyhow("ImageAsset: '{}' must be a non-empty flat 2D image", filename);
    if (spec.nchannels < 1 || spec.nchannels > 4)
        throw kira::Anyhow("ImageAsset: '{}' must contain one to four components", filename);
    if (spec.alpha_channel < -1 || spec.alpha_channel >= spec.nchannels)
        throw kira::Anyhow("ImageAsset: '{}' has an invalid alpha component", filename);
    if (spec.z_channel >= 0)
        throw kira::Anyhow("ImageAsset: '{}' must not contain a depth component", filename);
    if (spec.x != 0 || spec.y != 0 || spec.z != 0 || spec.full_x != 0 || spec.full_y != 0 ||
        spec.full_z != 0 || spec.full_width != spec.width || spec.full_height != spec.height ||
        spec.full_depth != spec.depth)
        throw kira::Anyhow(
            "ImageAsset: '{}' must have matching data and display windows at the origin", filename
        );
    auto const orientation = spec.get_int_attribute("Orientation", 1);
    if (orientation < 1 || orientation > 8)
        throw kira::Anyhow("ImageAsset: '{}' has an invalid orientation", filename);
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

[[nodiscard]] ImageComponentMapping makeFileComponentMapping(OIIO::ImageSpec const &spec) {
    auto const components = spec.nchannels;
    auto const alpha = spec.alpha_channel;
    if (components == 1 && alpha == 0)
        return {
            .r = ImageComponentSource::Zero,
            .g = ImageComponentSource::Zero,
            .b = ImageComponentSource::Zero,
            .a = ImageComponentSource::X,
        };
    if (components == 1)
        return {
            .r = ImageComponentSource::X,
            .g = ImageComponentSource::X,
            .b = ImageComponentSource::X,
            .a = ImageComponentSource::One,
        };
    if (components == 2 && alpha < 0)
        return {
            .r = ImageComponentSource::X,
            .g = ImageComponentSource::Y,
            .b = ImageComponentSource::Zero,
            .a = ImageComponentSource::One,
        };
    if (components == 2) {
        auto const color = static_cast<ImageComponentSource>(alpha == 0 ? 1 : 0);
        return {
            .r = color,
            .g = color,
            .b = color,
            .a = static_cast<ImageComponentSource>(alpha),
        };
    }
    if (components == 3 && alpha < 0)
        return {
            .r = ImageComponentSource::X,
            .g = ImageComponentSource::Y,
            .b = ImageComponentSource::Z,
            .a = ImageComponentSource::One,
        };
    if (components == 4 && alpha < 0)
        return {};

    auto color = std::array{
        ImageComponentSource::Zero,
        ImageComponentSource::Zero,
        ImageComponentSource::Zero,
    };
    auto colorIndex = std::size_t{};
    for (auto component = 0; component < components; ++component)
        if (component != alpha)
            color[colorIndex++] = static_cast<ImageComponentSource>(component);
    return {
        .r = color[0],
        .g = color[1],
        .b = components == 3 ? ImageComponentSource::Zero : color[2],
        .a = static_cast<ImageComponentSource>(alpha),
    };
}

[[nodiscard]] OIIO::ImageBuf convertToRGBA(
    OIIO::ImageBuf const &source, ImageComponentMapping mapping, std::string_view filename
) {
    auto const sources = std::array{mapping.r, mapping.g, mapping.b, mapping.a};
    auto order = std::array<int, 4>{};
    auto fill = std::array<float, 4>{};
    for (auto index = std::size_t{}; index < sources.size(); ++index) {
        auto const component = static_cast<std::uint8_t>(sources[index]);
        if (component <= static_cast<std::uint8_t>(ImageComponentSource::W)) {
            order[index] = component;
        } else {
            order[index] = -1;
            fill[index] = sources[index] == ImageComponentSource::One ? 1.0F : 0.0F;
        }
    }

    auto result = OIIO::ImageBuf{};
    if (!OIIO::ImageBufAlgo::channels(result, source, 4, order, fill))
        throw kira::Anyhow(
            "ImageAsset: failed to arrange components for '{}': {}", filename, result.geterror()
        );
    return result;
}
} // namespace

ImageAssetPool::pImpl::pImpl() {
    auto createTextureSystem = [](ImageColorSpace colorSpace) {
        auto imageCache = OIIO::ImageCache::create(/* shared = */ false);
        if (!imageCache)
            throw kira::Anyhow("ImageAssetPool: failed to create an OpenImageIO image cache");
        if (!imageCache->attribute("autotile", 64))
            throw kira::Anyhow("ImageAssetPool: failed to configure the OpenImageIO tile cache");
        if (!imageCache->attribute("colorspace", linearRec709))
            throw kira::Anyhow("ImageAssetPool: failed to configure the working color space");
        if (!imageCache->attribute("unassociatedalpha", 1))
            throw kira::Anyhow("ImageAssetPool: failed to configure straight alpha");
        if (colorSpace == ImageColorSpace::SRGB && !imageCache->attribute("forcefloat", 1))
            throw kira::Anyhow(
                "ImageAssetPool: failed to configure the sRGB image cache component type"
            );

        auto textureSystem =
            OIIO::TextureSystem::create(/* shared = */ false, std::move(imageCache));
        if (!textureSystem)
            throw kira::Anyhow("ImageAssetPool: failed to create an OpenImageIO texture system");
        if (!textureSystem->attribute("flip_t", 1))
            throw kira::Anyhow("ImageAssetPool: failed to configure texture coordinates");
        return textureSystem;
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
        throw kira::Anyhow("ImageAsset: failed to open '{}'", path.string());

    auto *imageHandle = imageCache->get_image_handle(filename);
    if (!imageHandle || !imageCache->good(imageHandle))
        throw kira::Anyhow("ImageAsset: failed to open '{}'", path.string());

    auto spec = OIIO::ImageSpec{};
    if (!imageCache->get_imagespec(
            imageHandle, /* threadInfo = */ nullptr, spec, /* subimage = */ 0
        ))
        throw kira::Anyhow("ImageAsset: failed to read metadata for '{}'", path.string());
    validateImageSpec(spec, path.string());

    auto const hasRGB = spec.nchannels == 3 && spec.alpha_channel < 0;
    auto const hasRGBA = spec.nchannels == 4 && spec.alpha_channel >= 0;
    if (colorSpace == ImageColorSpace::SRGB && !hasRGB && !hasRGBA)
        throw kira::Anyhow(
            "ImageAsset: sRGB input '{}' must contain RGB or RGBA pixels", path.string()
        );

    auto width = spec.width;
    auto height = spec.height;
    orientation = static_cast<std::uint8_t>(spec.get_int_attribute("Orientation", 1));
    if (orientation >= 5 && orientation <= 8)
        std::swap(width, height);
    extent = Vec2u{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    fileComponentCount = static_cast<std::uint8_t>(spec.nchannels);
    componentType = chooseComponentType(spec);
    auto const fileComponentMapping = makeFileComponentMapping(spec);
    if (fileComponentCount >= 3 && fileComponentMapping != ImageComponentMapping{})
        fileToRGBA = fileComponentMapping;
    componentCount = fileToRGBA ? 4 : fileComponentCount;
    defaultComponentMapping = fileToRGBA ? ImageComponentMapping{} : fileComponentMapping;

    if (colorSpace == ImageColorSpace::SRGB) {
        colorTransformId = textureSystem->get_colortransform_id(
            OIIO::ustring{srgbRec709}, OIIO::ustring{linearRec709}
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

ImageAsset::ImageAsset(std::unique_ptr<pImpl> pImpl) noexcept : pImpl_(std::move(pImpl)) {}
ImageAsset::~ImageAsset() = default;

Vec2u ImageAsset::getExtent() const noexcept { return pImpl_->extent; }
std::uint8_t ImageAsset::getComponentCount() const noexcept { return pImpl_->componentCount; }
ImageComponentType ImageAsset::getComponentType() const noexcept { return pImpl_->componentType; }
ImageComponentMapping ImageAsset::getDefaultComponentMapping() const noexcept {
    return pImpl_->defaultComponentMapping;
}

ImageAsset::ImageBuffer ImageAsset::read() const {
    auto source =
        OIIO::ImageBuf{pImpl_->filename.string(), 0, 0, pImpl_->textureSystem->imagecache()};
    auto const format = [this] {
        switch (pImpl_->componentType) {
        case ImageComponentType::UNorm8: return OIIO::TypeDesc::UINT8;
        case ImageComponentType::Float16: return OIIO::TypeDesc::HALF;
        case ImageComponentType::Float32: return OIIO::TypeDesc::FLOAT;
        }
        KIRA_UNREACHABLE();
    }();
    if (!source.read(0, 0, true, format))
        throw kira::Anyhow(
            "ImageAsset: failed to read '{}': {}", pImpl_->filename.string(), source.geterror()
        );

    if (pImpl_->fileToRGBA)
        source = convertToRGBA(source, *pImpl_->fileToRGBA, pImpl_->filename.string());

    // UNorm8 keeps its sRGB values. Float16 and Float32 pixels are converted
    // to linear without changing their component type.
    auto colorSpace = ImageColorSpace::Linear;
    if (pImpl_->fileColorSpace == ImageColorSpace::SRGB &&
        pImpl_->componentType == ImageComponentType::UNorm8) {
        colorSpace = ImageColorSpace::SRGB;
    } else if (pImpl_->fileColorSpace == ImageColorSpace::SRGB) {
        if (!OIIO::ImageBufAlgo::colorconvert(
                source, source, srgbRec709, linearRec709, /* unpremult = */ false
            ))
            throw kira::Anyhow(
                "ImageAsset: failed to convert '{}' from sRGB to linear: {}",
                pImpl_->filename.string(), source.geterror()
            );
    }

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
    if (std::cmp_not_equal(image.spec().width, pImpl_->extent.x()) ||
        std::cmp_not_equal(image.spec().height, pImpl_->extent.y()) ||
        std::cmp_not_equal(image.spec().nchannels, pImpl_->componentCount))
        throw kira::Anyhow(
            "ImageAsset: '{}' changed after the asset was created", pImpl_->filename.string()
        );
    if (!image.localpixels())
        throw kira::Anyhow(
            "ImageAsset: failed to store '{}' in host memory", pImpl_->filename.string()
        );

    return {
        .image = std::move(image),
        .extent = pImpl_->extent,
        .componentCount = pImpl_->componentCount,
        .componentType = pImpl_->componentType,
        .colorSpace = colorSpace,
    };
}

ImageAssetPool::ImageAssetPool() : pImpl_(std::make_unique<pImpl>()) {}
ImageAssetPool::~ImageAssetPool() = default;

Ref<ImageAsset const> ImageAssetPool::getOrCreate(ImageAssetRequest const &request) {
    auto key = request;
    key.path = std::filesystem::weakly_canonical(std::filesystem::absolute(request.path));
    return assets_.acquire(key, [&] {
        auto const &textureSystem = key.colorSpace == ImageColorSpace::SRGB
                                        ? pImpl_->srgbTextureSystem
                                        : pImpl_->linearTextureSystem;
        return Ref<ImageAsset const>{new ImageAsset(
            std::make_unique<ImageAsset::pImpl>(textureSystem, key.path, key.colorSpace)
        )};
    });
}
} // namespace flux
