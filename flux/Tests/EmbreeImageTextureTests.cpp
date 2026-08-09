#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
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
        std::string_view filterMode = "point", std::string_view addressMode = "wrap",
        std::string_view componentMapping = {}
    ) {
        kira::Properties props;
        props.set("type", "image");
        props.set("path", imagePath);
        props.set("color_space", std::string{colorSpace});
        props.set("filter_mode", std::string{filterMode});
        props.set("address_mode", std::string{addressMode});
        if (!componentMapping.empty())
            props.set("component_mapping", std::string{componentMapping});
        (void)context->create<flux::Texture>(props);
        pool.build(*context);
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

struct ImageOrientationCase {
    char const *name;
    int orientation;
    flux::Vec2u extent;
    std::array<float, 6> bottomUp;
};

class EmbreeImageOrientationTests : public EmbreeImageTextureTests,
                                    public testing::WithParamInterface<ImageOrientationCase> {};
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

TEST_F(EmbreeImageTextureTests, PreservesStraightAlphaDuringSRGBConversion) {
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
    build(imagePath, "srgb");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 1.0F, 2.0e-3F);
    EXPECT_NEAR(value.y(), 0.21404114F, 2.0e-3F);
    EXPECT_NEAR(value.z(), 0.05087609F, 2.0e-3F);
    EXPECT_NEAR(value.w(), 128.0F / 255.0F, 2.0e-3F);
}

TEST_P(EmbreeImageOrientationTests, AppliesImageOrientation) {
    auto const &parameter = GetParam();
    auto const imagePath = path(std::string{parameter.name} + ".exr");
    auto const componentNames = std::array<std::string_view, 1>{"Y"};
    auto const pixels = std::array{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    flux::writeImage(
        imagePath,
        {
            .pixels = std::as_bytes(std::span{pixels}),
            .extent = {3, 2},
            .componentType = flux::ImageComponentType::Float32,
            .componentCount = static_cast<std::uint8_t>(componentNames.size()),
        },
        {
            .outputComponentType = flux::ImageComponentType::Float32,
            .componentNames = componentNames,
            .orientation = parameter.orientation,
        }
    );
    build(imagePath);

    EXPECT_EQ(
        context->getObjects<flux::ImageTexture>().front()->getImageAsset()->getExtent(),
        parameter.extent
    );
    for (auto y = std::uint32_t{}; y < parameter.extent.y(); ++y) {
        for (auto x = std::uint32_t{}; x < parameter.extent.x(); ++x) {
            SCOPED_TRACE(testing::Message{} << "pixel = (" << x << ", " << y << ')');
            auto const uv = flux::Vec2f{
                (static_cast<float>(x) + 0.5F) / static_cast<float>(parameter.extent.x()),
                (static_cast<float>(y) + 0.5F) / static_cast<float>(parameter.extent.y()),
            };
            EXPECT_FLOAT_EQ(sample(uv).x(), parameter.bottomUp[y * parameter.extent.x() + x]);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    Orientations, EmbreeImageOrientationTests,
    testing::Values(
        ImageOrientationCase{"Identity", 1, {3, 2}, {4, 5, 6, 1, 2, 3}},
        ImageOrientationCase{"MirrorHorizontal", 2, {3, 2}, {6, 5, 4, 3, 2, 1}},
        ImageOrientationCase{"Rotate180", 3, {3, 2}, {3, 2, 1, 6, 5, 4}},
        ImageOrientationCase{"MirrorVertical", 4, {3, 2}, {1, 2, 3, 4, 5, 6}},
        ImageOrientationCase{"Transpose", 5, {2, 3}, {4, 1, 5, 2, 6, 3}},
        ImageOrientationCase{"Rotate90", 6, {2, 3}, {6, 3, 5, 2, 4, 1}},
        ImageOrientationCase{"Transverse", 7, {2, 3}, {3, 6, 2, 5, 1, 4}},
        ImageOrientationCase{"Rotate270", 8, {2, 3}, {1, 4, 2, 5, 3, 6}}
    ),
    [](testing::TestParamInfo<ImageOrientationCase> const &info) { return info.param.name; }
);

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
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGBFilter2x2.ppm", "srgb", "linear");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.28918776F, 1.5e-3F);
    EXPECT_NEAR(value.y(), 0.28918776F, 1.5e-3F);
    EXPECT_NEAR(value.z(), 0.28918776F, 1.5e-3F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(EmbreeImageTextureTests, DecodesUNorm8SRGB) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGB.ppm", "srgb");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.03310477F, 2.5e-4F);
    EXPECT_NEAR(value.y(), 0.13286832F, 2.5e-4F);
    EXPECT_NEAR(value.z(), 0.31854678F, 2.5e-4F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(EmbreeImageTextureTests, WrapsCoordinates) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb");

    EXPECT_EQ(sample({1.25F, 0.25F}), sample({0.25F, 0.25F}));
}

TEST_F(EmbreeImageTextureTests, FiltersAcrossBorder) {
    build(
        std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "linear", "linear",
        "border"
    );

    EXPECT_EQ(sample({-0.25F, 0.25F}), flux::Vec4f{});
    expectNear(sample({0.0F, 0.25F}), {0.0F, 0.0F, 0.5F, 0.5F});
}

TEST_F(EmbreeImageTextureTests, AppliesComponentMappingAfterBorderFiltering) {
    build(
        std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "linear", "linear",
        "border", "1x0w"
    );

    EXPECT_EQ(sample({-0.25F, 0.25F}), (flux::Vec4f{1.0F, 0.0F, 0.0F, 0.0F}));
    expectNear(sample({0.0F, 0.25F}), {1.0F, 0.0F, 0.0F, 0.5F});
}
