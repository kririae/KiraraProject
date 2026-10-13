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
