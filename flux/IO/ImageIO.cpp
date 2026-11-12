#include "flux/IO/ImageIO.h"

#include <OpenImageIO/imagebuf.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

#include "kira/Anyhow.h"

namespace flux {
namespace {
[[nodiscard]] constexpr std::size_t componentSize(ImageComponentType type) noexcept {
    switch (type) {
    case ImageComponentType::UNorm8: return 1;
    case ImageComponentType::Float16: return 2;
    case ImageComponentType::Float32: return 4;
    }
    KIRA_UNREACHABLE();
}

[[nodiscard]] constexpr OIIO::TypeDesc oiioType(ImageComponentType type) noexcept {
    switch (type) {
    case ImageComponentType::UNorm8: return OIIO::TypeDesc::UINT8;
    case ImageComponentType::Float16: return OIIO::TypeDesc::HALF;
    case ImageComponentType::Float32: return OIIO::TypeDesc::FLOAT;
    }
    return OIIO::TypeDesc::UNKNOWN;
}
} // namespace

void writeImage(std::filesystem::path const &path, ImageView image, ImageWriteOptions options) {
    if (image.extent.x() == 0 || image.extent.y() == 0 || image.componentCount == 0)
        throw kira::Anyhow("ImageIO: image extent and component count must be positive");
    if (image.extent.x() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        image.extent.y() > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        throw kira::Anyhow("ImageIO: image dimensions exceed OpenImageIO limits");
    if (!options.componentNames.empty() && options.componentNames.size() != image.componentCount)
        throw kira::Anyhow("ImageIO: component name count does not match the image");
    if (options.orientation < 1 || options.orientation > 8)
        throw kira::Anyhow("ImageIO: orientation must be between 1 and 8");

    auto const width = static_cast<std::size_t>(image.extent.x());
    auto const height = static_cast<std::size_t>(image.extent.y());
    if (height > std::numeric_limits<std::size_t>::max() / width)
        throw kira::Anyhow("ImageIO: image size exceeds host limits");
    auto const pixelCount = width * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / image.componentCount ||
        pixelCount * image.componentCount >
            std::numeric_limits<std::size_t>::max() / componentSize(image.componentType) ||
        image.pixels.size() !=
            pixelCount * image.componentCount * componentSize(image.componentType))
        throw kira::Anyhow("ImageIO: pixel buffer size does not match the image");

    auto spec = OIIO::ImageSpec{
        static_cast<int>(image.extent.x()),
        static_cast<int>(image.extent.y()),
        image.componentCount,
        oiioType(image.componentType),
    };
    if (!options.componentNames.empty())
        spec.channelnames.assign(options.componentNames.begin(), options.componentNames.end());
    else if (image.componentCount == 1)
        spec.channelnames.front() = "Y";

    if (std::ranges::count(spec.channelnames, "A") > 1)
        throw kira::Anyhow("ImageIO: component names contain more than one alpha component");
    spec.alpha_channel = -1;
    auto const alpha = std::ranges::find(spec.channelnames, "A");
    if (alpha != spec.channelnames.end()) {
        spec.alpha_channel = static_cast<int>(alpha - spec.channelnames.begin());
        spec.attribute("oiio:UnassociatedAlpha", 1);
    }
    if (options.orientation != 1)
        spec.attribute("Orientation", options.orientation);

    auto const parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    auto output =
        OIIO::ImageBuf{spec, OIIO::cspan<std::byte>{image.pixels.data(), image.pixels.size()}};
    output.set_write_format(oiioType(options.outputComponentType));
    if (!output.write(path.string()))
        throw kira::Anyhow("ImageIO: failed to write '{}': {}", path.string(), output.geterror());
}

namespace detail {
void writeExr(std::filesystem::path const &path, ImageView image) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (extension != ".exr")
        throw kira::Anyhow("ImageIO: output path '{}' is not an EXR file", path.string());
    writeImage(path, image, {.outputComponentType = ImageComponentType::Float32});
}
} // namespace detail
} // namespace flux
