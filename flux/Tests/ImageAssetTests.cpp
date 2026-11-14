#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include "InMemoryFileSystem.h"
#include "TestUtils.h"
#include "flux/Core/ResolvedPath.h"
#include "flux/Scene/ImageAsset.h"

namespace {
class ImageAssetTests : public testing::Test {
protected:
    [[nodiscard]] static std::filesystem::path path(std::string_view filename) {
        return std::filesystem::path{FLUX_TEST_OUTPUT_DIR} / filename;
    }

    flux::ImageAssetPool pool;
};
} // namespace

TEST_F(ImageAssetTests, LoadsMetadataAndSharesTheSameName) {
    auto const imagePath = path("image-asset.exr");
    auto const componentNames = std::array<std::string_view, 3>{"R", "G", "B"};
    auto const pixels = std::array{
        1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F,
    };
    flux::test::writeFloatImage(imagePath, pixels, {2, 2}, componentNames);

    auto const asset =
        pool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});
    auto const cached =
        pool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});

    EXPECT_EQ(asset, cached);
    EXPECT_EQ(asset->getExtent(), (flux::Vec2u{2, 2}));
    EXPECT_EQ(asset->getComponentCount(), 4);
    EXPECT_EQ(asset->getComponentType(), flux::ImageComponentType::Float32);
}

TEST_F(ImageAssetTests, UsesColorSpaceInPoolKey) {
    auto const imagePath = std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGB.ppm";
    auto const linear =
        pool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});
    auto const srgb = pool.getOrCreate({
        .resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)},
        .colorSpace = std::string{flux::srgbTexture},
    });

    EXPECT_NE(linear, srgb);
    EXPECT_NE(linear->getId(), srgb->getId());
    EXPECT_EQ(
        pool.getOrCreate(
            {.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)},
             .colorSpace = std::string{flux::autoColorSpace}}
        ),
        srgb
    );
}

TEST_F(ImageAssetTests, SharesOneAssetAmongTheNamesOfLinearRec709) {
    auto const imagePath = path("linear-names.exr");
    auto const pixels = std::array{0.5F, 0.25F, 0.125F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1});

    auto const unnamed =
        pool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});
    auto const unknown = pool.getOrCreate(
        {.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)},
         .colorSpace = "no-such-color-space"}
    );
    auto const named = pool.getOrCreate(
        {.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)},
         .colorSpace = std::string{flux::linearRec709}}
    );

    EXPECT_EQ(unnamed, named);
    EXPECT_EQ(unknown, named);
}

TEST_F(ImageAssetTests, ChoosesSRGBTextureForUNorm8WithAtLeastThreeChannels) {
    struct Case {
        std::uint8_t channels;
        flux::ImageComponentType type;
        std::string_view expected;
    };
    using flux::ImageComponentType;
    constexpr auto srgb = flux::srgbTexture;
    constexpr auto linear = flux::linearRec709;
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    auto const cases = std::array{
        Case{1, ImageComponentType::UNorm8, linear},  Case{2, ImageComponentType::UNorm8, linear},
        Case{3, ImageComponentType::UNorm8, srgb},    Case{4, ImageComponentType::UNorm8, srgb},
        Case{3, ImageComponentType::Float16, linear}, Case{3, ImageComponentType::Float32, linear},
    };
    for (auto const &[channels, type, expected] : cases) {
        SCOPED_TRACE(
            testing::Message{} << "channels = " << static_cast<int>(channels)
                               << ", type = " << static_cast<int>(type)
        );
        auto const extension = type == ImageComponentType::UNorm8 ? ".tif" : ".exr";
        auto const imagePath = path(
            "color-space-" + std::to_string(channels) + "-" +
            std::to_string(static_cast<int>(type)) + extension
        );
        flux::test::writeFloatImage(
            imagePath, {pixels.data(), channels}, {1, 1}, {componentNames.data(), channels}, type
        );

        EXPECT_EQ(pool.chooseColorSpace(flux::ResolvedPath{flux::u8string(imagePath)}), expected);
    }
}

TEST_F(ImageAssetTests, DefaultMappingFollowsChannelPosition) {
    using Source = flux::ImageComponentSource;
    // Alpha-like names do not change the mapping.
    auto const componentNames = std::array<std::string_view, 4>{"A", "R", "G", "B"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    auto const expected = std::array{
        flux::ImageComponentMapping{
            .r = Source::X, .g = Source::X, .b = Source::X, .a = Source::One
        },
        flux::ImageComponentMapping{.r = Source::X, .g = Source::X, .b = Source::X, .a = Source::Y},
        flux::ImageComponentMapping{},
        flux::ImageComponentMapping{},
    };
    for (auto count = std::uint8_t{1}; count <= 4; ++count) {
        SCOPED_TRACE(testing::Message{} << "components = " << static_cast<int>(count));
        auto const imagePath = path("channels-" + std::to_string(count) + ".exr");
        flux::test::writeFloatImage(
            imagePath, {pixels.data(), count}, {1, 1}, {componentNames.data(), count}
        );

        auto const asset =
            pool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});
        EXPECT_EQ(asset->getComponentCount(), count == 3 ? 4 : count);
        EXPECT_EQ(asset->getDefaultComponentMapping(), expected[count - 1]);
    }
}

TEST_F(ImageAssetTests, CollectsAssetsThatOnlyThePoolHolds) {
    auto const imagePath = path("image-asset-collect.exr");
    auto const componentNames = std::array<std::string_view, 1>{"Y"};
    auto const pixels = std::array{0.5F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1}, componentNames);

    auto asset = pool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});
    pool.collectGarbage();
    EXPECT_EQ(pool.size(), 1U);

    asset.reset();
    pool.collectGarbage();
    EXPECT_EQ(pool.size(), 0U);
}

namespace {
void writeMemoryFixture(std::filesystem::path const &imagePath) {
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    flux::test::writeFloatImage(
        imagePath, pixels, {2, 1}, std::array<std::string_view, 2>{"Y", "A"},
        flux::ImageComponentType::Float16
    );
}
} // namespace

TEST_F(ImageAssetTests, GivesTheEmptyAssetForANameThatTheFileSystemCannotOpen) {
    auto const empty = pool.getOrCreate({});
    auto fileSystem = flux::test::InMemoryFileSystem{};
    auto memoryPool = flux::ImageAssetPool{&fileSystem};

    auto const missing =
        memoryPool.getOrCreate({.resolvedPath = flux::ResolvedPath{"memory/missing.exr"}});

    EXPECT_EQ(fileSystem.getOpenCount(), 1);
    EXPECT_EQ(missing->getExtent(), empty->getExtent());
    EXPECT_EQ(missing->getComponentCount(), empty->getComponentCount());
    EXPECT_EQ(missing->getComponentType(), empty->getComponentType());
}

TEST_F(ImageAssetTests, OpensARegularFileByPathWithAFileSystem) {
    auto const imagePath = path("memory-fixture-file.exr");
    writeMemoryFixture(imagePath);
    auto fileSystem = flux::test::InMemoryFileSystem{};
    auto memoryPool = flux::ImageAssetPool{&fileSystem};

    auto const asset =
        memoryPool.getOrCreate({.resolvedPath = flux::ResolvedPath{flux::u8string(imagePath)}});

    EXPECT_EQ(fileSystem.getOpenCount(), 0);
    EXPECT_EQ(asset->getExtent(), (flux::Vec2u{2, 1}));
}

TEST_F(ImageAssetTests, ChoosesTheColorSpaceOfANameThatIsNotAFileFromItsHeader) {
    auto const imagePath = path("memory-rgb8.tif");
    auto const pixels = std::array{0.25F, 0.5F, 0.75F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1}, {}, flux::ImageComponentType::UNorm8);
    auto fileSystem = flux::test::InMemoryFileSystem{};
    fileSystem.addFile(flux::ResolvedPath{"memory/a.tif"}, imagePath);
    auto memoryPool = flux::ImageAssetPool{&fileSystem};

    EXPECT_EQ(memoryPool.chooseColorSpace(flux::ResolvedPath{"memory/a.tif"}), flux::srgbTexture);
    EXPECT_EQ(
        memoryPool.chooseColorSpace(flux::ResolvedPath{"memory/missing.tif"}), flux::linearRec709
    );
}
