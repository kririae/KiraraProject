#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>

#include "flux/Hydra/ArFileSystem.h"

namespace {
[[nodiscard]] std::string_view toStringView(std::span<std::byte const> bytes) {
    return {reinterpret_cast<char const *>(bytes.data()), bytes.size()};
}
} // namespace

TEST(ArFileSystemTests, ReadsOnlyTheBytesBeforeTheEnd) {
    constexpr auto content = std::string_view{"0123456789"};
    auto const directory = std::filesystem::path{HDFLUX_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(directory);
    auto const path = directory / "content.bin";
    std::ofstream{path, std::ios::binary}.write(
        content.data(), static_cast<std::streamsize>(content.size())
    );

    auto const file = HdFLuxArFileSystem{}.open(flux::ResolvedPath{flux::u8string(path)});
    ASSERT_TRUE(file);

    auto buffer = std::array<std::byte, 8>{};
    EXPECT_EQ(file->read(buffer, 7), 3U);
    EXPECT_EQ(toStringView(std::span{buffer}.first(3)), content.substr(7));
    EXPECT_EQ(file->read(buffer, content.size()), 0U);
    EXPECT_EQ(file->read(buffer, 1000), 0U);
}
