#include "ImageWithUnreadableMipLevel.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfTiledOutputFile.h>

#include <array>
#include <filesystem>

namespace flux::test {
void writeImageWithUnreadableMipLevel(std::filesystem::path const &path) {
    auto const parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    constexpr auto extent = 4;
    constexpr auto tileExtent = 2;
    constexpr auto channelCount = 3;
    auto header = Imf::Header{extent, extent};
    for (auto const *name : {"R", "G", "B"})
        header.channels().insert(name, Imf::Channel{Imf::FLOAT});
    header.setTileDescription(Imf::TileDescription{tileExtent, tileExtent, Imf::MIPMAP_LEVELS});

    auto texels = std::array<float, extent * extent * channelCount>{};
    texels.fill(0.5F);
    auto frameBuffer = Imf::FrameBuffer{};
    auto const xStride = channelCount * sizeof(float);
    auto const yStride = xStride * extent;
    auto channel = 0;
    for (auto const *name : {"R", "G", "B"})
        frameBuffer.insert(
            name,
            Imf::Slice{
                Imf::FLOAT, reinterpret_cast<char *>(texels.data() + channel++), xStride, yStride
            }
        );

    auto writer = Imf::TiledOutputFile{path.string().c_str(), header};
    writer.setFrameBuffer(frameBuffer);
    for (auto level = 0; level < writer.numLevels() - 1; ++level)
        writer.writeTiles(0, writer.numXTiles(level) - 1, 0, writer.numYTiles(level) - 1, level);
}
} // namespace flux::test
