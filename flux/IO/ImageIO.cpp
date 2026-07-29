#include "flux/IO/ImageIO.h"

#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

#include "kira/Anyhow.h"

namespace flux::detail {
void writeExr(
    std::filesystem::path const &path, std::uint32_t width, std::uint32_t height,
    void const *pixels, std::size_t pixelCount, int components
) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (extension != ".exr")
        throw kira::Anyhow("ImageIO: output path '{}' is not an EXR file", path.string());
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        throw kira::Anyhow("ImageIO: image dimensions exceed OpenImageIO limits");
    if (width == 0 || height == 0 || !pixels ||
        height > std::numeric_limits<std::size_t>::max() / width ||
        pixelCount != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
        throw kira::Anyhow("ImageIO: film channel has not been downloaded");

    auto const parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    auto const filename = path.string();
    auto output = OIIO::ImageOutput::create(filename);
    if (!output)
        throw kira::Anyhow("ImageIO: failed to create output '{}': {}", filename, OIIO::geterror());

    OIIO::ImageSpec specification{
        static_cast<int>(width),
        static_cast<int>(height),
        components,
        OIIO::TypeDesc::FLOAT,
    };
    if (components == 1)
        specification.channelnames.front() = "Y";
    if (!output->open(filename, specification))
        throw kira::Anyhow("ImageIO: failed to open output '{}': {}", filename, output->geterror());
    if (!output->write_image(OIIO::TypeDesc::FLOAT, pixels)) {
        auto const error = output->geterror();
        (void)output->close();
        throw kira::Anyhow("ImageIO: failed to write output '{}': {}", filename, error);
    }
    if (!output->close())
        throw kira::Anyhow(
            "ImageIO: failed to close output '{}': {}", filename, output->geterror()
        );
}
} // namespace flux::detail
