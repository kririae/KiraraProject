#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

#include "flux/Embree/EmbreeImageTexturePool.h"
#include "flux/IO/ImageIO.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"

namespace {
class EmbreeImageTextureTests : public testing::Test {
protected:
    [[nodiscard]] static std::filesystem::path path(std::string_view filename) {
        return std::filesystem::path{FLUX_TEST_OUTPUT_DIR} / filename;
    }

    void build(
        std::filesystem::path const &imagePath, std::string_view colorSpace = "linear",
        std::string_view filterMode = "point", std::string_view addressMode = "wrap"
    ) {
        kira::Properties props;
        props.set("type", "image");
        props.set("path", imagePath);
        props.set("color_space", std::string{colorSpace});
        props.set("filter_mode", std::string{filterMode});
        props.set("address_mode", std::string{addressMode});
        (void)context->create<flux::Texture>(props);
        pool.build(context->getObjects<flux::ImageTexture>());
    }

    [[nodiscard]] flux::Vec4f sample(flux::Vec2f uv) const noexcept {
        return pool.getImpl().eval4f(0, uv);
    }

    static void expectNear(flux::Vec4f const &actual, flux::Vec4f const &expected) {
        constexpr auto tolerance = 3.0e-5F;
        EXPECT_NEAR(actual.x(), expected.x(), tolerance);
        EXPECT_NEAR(actual.y(), expected.y(), tolerance);
        EXPECT_NEAR(actual.z(), expected.z(), tolerance);
        EXPECT_NEAR(actual.w(), expected.w(), tolerance);
    }

    flux::Ref<flux::Context> context = flux::Context::create();
    flux::EmbreeImageTexturePool pool;
};
} // namespace

TEST_F(EmbreeImageTextureTests, ConvertsNamedSRGBComponents) {
    auto const imagePath = path("argb-srgb.exr");
    auto const componentNames = std::array<std::string_view, 4>{"A", "R", "G", "B"};
    auto const pixels = std::array{0.4F, 0.5F, 0.25F, 0.75F};
    flux::writeImage(
        imagePath,
        {
            .pixels = std::as_bytes(std::span{pixels}),
            .extent = {1, 1},
            .componentType = flux::ImageComponentType::Float32,
            .componentCount = static_cast<std::uint8_t>(componentNames.size()),
        },
        {
            .outputComponentType = flux::ImageComponentType::Float32,
            .componentNames = componentNames,
        }
    );
    build(imagePath, "srgb");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.21404114F, 2.0e-5F);
    EXPECT_NEAR(value.y(), 0.05087609F, 2.0e-5F);
    EXPECT_NEAR(value.z(), 0.52252155F, 2.0e-5F);
    EXPECT_FLOAT_EQ(value.w(), 0.4F);
}

TEST_F(EmbreeImageTextureTests, PreservesStraightAlpha) {
    auto const imagePath = path("straight-alpha.tif");
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{1.0F, 0.5F, 0.25F, 0.5F};
    flux::writeImage(
        imagePath,
        {
            .pixels = std::as_bytes(std::span{pixels}),
            .extent = {1, 1},
            .componentType = flux::ImageComponentType::Float32,
            .componentCount = static_cast<std::uint8_t>(componentNames.size()),
        },
        {
            .outputComponentType = flux::ImageComponentType::UNorm8,
            .componentNames = componentNames,
        }
    );
    build(imagePath);

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 1.0F, 1.0e-5F);
    EXPECT_NEAR(value.y(), 128.0F / 255.0F, 1.0e-5F);
    EXPECT_NEAR(value.z(), 64.0F / 255.0F, 1.0e-5F);
    EXPECT_NEAR(value.w(), 128.0F / 255.0F, 1.0e-5F);
}

TEST_F(EmbreeImageTextureTests, AppliesImageOrientation) {
    auto const imagePath = path("oriented.tif");
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F};
    flux::writeImage(
        imagePath,
        {
            .pixels = std::as_bytes(std::span{pixels}),
            .extent = {2, 1},
            .componentType = flux::ImageComponentType::Float32,
            .componentCount = static_cast<std::uint8_t>(componentNames.size()),
        },
        {
            .outputComponentType = flux::ImageComponentType::UNorm8,
            .componentNames = componentNames,
            .orientation = 6,
        }
    );
    build(imagePath);

    EXPECT_EQ(
        context->getObjects<flux::ImageTexture>().front()->getImageAsset()->getExtent(),
        (flux::Vec2u{1, 2})
    );
    EXPECT_EQ(sample({0.5F, 0.25F}), (flux::Vec4f{0.0F, 1.0F, 0.0F, 1.0F}));
    EXPECT_EQ(sample({0.5F, 0.75F}), (flux::Vec4f{1.0F, 0.0F, 0.0F, 1.0F}));
}

TEST_F(EmbreeImageTextureTests, SamplesBottomUpCoordinates) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb");

    auto const samples = std::array{
        std::pair{flux::Vec2f{0.25F, 0.25F}, flux::Vec4f{0.0F, 0.0F, 1.0F, 1.0F}},
        std::pair{flux::Vec2f{0.75F, 0.25F}, flux::Vec4f{1.0F, 1.0F, 1.0F, 1.0F}},
        std::pair{flux::Vec2f{0.25F, 0.75F}, flux::Vec4f{1.0F, 0.0F, 0.0F, 1.0F}},
        std::pair{flux::Vec2f{0.75F, 0.75F}, flux::Vec4f{0.0F, 1.0F, 0.0F, 1.0F}},
    };
    for (auto const &[uv, expected] : samples) {
        SCOPED_TRACE(testing::Message{} << "uv = (" << uv.x() << ", " << uv.y() << ')');
        expectNear(sample(uv), expected);
    }
}

TEST_F(EmbreeImageTextureTests, FiltersLinearSRGBValues) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb", "linear");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.5F, 3.0e-5F);
    EXPECT_NEAR(value.y(), 0.5F, 3.0e-5F);
    EXPECT_NEAR(value.z(), 0.5F, 3.0e-5F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(EmbreeImageTextureTests, WrapsCoordinates) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb");

    EXPECT_EQ(sample({1.25F, 0.25F}), sample({0.25F, 0.25F}));
}
