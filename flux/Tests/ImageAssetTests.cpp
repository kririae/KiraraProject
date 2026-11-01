#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string_view>

#include "TestUtils.h"
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

TEST_F(ImageAssetTests, LoadsMetadataAndSharesCanonicalPaths) {
    auto const imagePath = path("image-asset.exr");
    auto const componentNames = std::array<std::string_view, 3>{"R", "G", "B"};
    auto const pixels = std::array{
        1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F,
    };
    flux::test::writeFloatImage(imagePath, pixels, {2, 2}, componentNames);

    auto const asset = pool.getOrCreate({.path = imagePath});
    auto const cached =
        pool.getOrCreate({.path = imagePath.parent_path() / "." / imagePath.filename()});

    EXPECT_EQ(asset, cached);
    EXPECT_EQ(asset->getExtent(), (flux::Vec2u{2, 2}));
    EXPECT_EQ(asset->getComponentCount(), 4);
    EXPECT_EQ(asset->getComponentType(), flux::ImageComponentType::Float32);
}

TEST_F(ImageAssetTests, UsesColorSpaceInPoolKey) {
    auto const imagePath = std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGB.ppm";
    auto const linear = pool.getOrCreate({.path = imagePath});
    auto const srgb = pool.getOrCreate({
        .path = imagePath,
        .colorSpace = flux::ImageColorSpace::SRGB,
    });

    EXPECT_NE(linear, srgb);
    EXPECT_NE(linear->getId(), srgb->getId());
}

TEST_F(ImageAssetTests, ChoosesSRGBForUNorm8WithAtLeastThreeChannels) {
    struct Case {
        std::uint8_t channels;
        flux::ImageComponentType type;
        flux::ImageColorSpace expected;
    };
    using flux::ImageColorSpace;
    using flux::ImageComponentType;
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    auto const cases = std::array{
        Case{1, ImageComponentType::UNorm8, ImageColorSpace::Linear},
        Case{2, ImageComponentType::UNorm8, ImageColorSpace::Linear},
        Case{3, ImageComponentType::UNorm8, ImageColorSpace::SRGB},
        Case{4, ImageComponentType::UNorm8, ImageColorSpace::SRGB},
        Case{3, ImageComponentType::Float16, ImageColorSpace::Linear},
        Case{3, ImageComponentType::Float32, ImageColorSpace::Linear},
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

        EXPECT_EQ(pool.chooseColorSpace(imagePath), expected);
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

        auto const asset = pool.getOrCreate({.path = imagePath});
        EXPECT_EQ(asset->getComponentCount(), count == 3 ? 4 : count);
        EXPECT_EQ(asset->getDefaultComponentMapping(), expected[count - 1]);
    }
}

TEST_F(ImageAssetTests, CollectsAssetsThatOnlyThePoolHolds) {
    auto const imagePath = path("image-asset-collect.exr");
    auto const componentNames = std::array<std::string_view, 1>{"Y"};
    auto const pixels = std::array{0.5F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1}, componentNames);

    auto asset = pool.getOrCreate({.path = imagePath});
    pool.collectGarbage();
    EXPECT_EQ(pool.size(), 1U);

    asset.reset();
    pool.collectGarbage();
    EXPECT_EQ(pool.size(), 0U);
}
