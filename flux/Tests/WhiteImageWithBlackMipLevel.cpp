#include "WhiteImageWithBlackMipLevel.h"

#include <OpenImageIO/imageio.h>

#include <array>
#include <filesystem>
#include <stdexcept>

namespace flux::test {
void writeWhiteImageWithBlackMipLevel(std::filesystem::path const &path) {
    auto const parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    auto spec = OIIO::ImageSpec{2, 2, 3, OIIO::TypeDesc::FLOAT};
    spec.tile_width = spec.tile_height = 2;
    spec.attribute("textureformat", "Plain Texture");
    auto const output = OIIO::ImageOutput::create(path.string());
    auto const white =
        std::array{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    auto const black = std::array{0.0F, 0.0F, 0.0F};
    if (!output->open(path.string(), spec) ||
        !output->write_image(OIIO::TypeDesc::FLOAT, white.data()))
        throw std::runtime_error{output->geterror()};
    spec.width = spec.height = 1;
    if (!output->open(path.string(), spec, OIIO::ImageOutput::AppendMIPLevel) ||
        !output->write_image(OIIO::TypeDesc::FLOAT, black.data()) || !output->close())
        throw std::runtime_error{output->geterror()};
}
} // namespace flux::test
