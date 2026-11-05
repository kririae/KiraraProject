#include <OpenImageIO/imagebuf.h>
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include "TestUtils.h"
#include "flux/Embree/EmbreeImageTexturePool.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

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
        build(props);
    }

    void build(kira::Properties const &props) {
        (void)context->create<flux::Texture>(props);
        pool.build(*context);
    }

    [[nodiscard]] flux::Vec4f
    sample(flux::Vec2f uv, flux::Vec2f duvdx = {}, flux::Vec2f duvdy = {}) const noexcept {
        return pool.getImpl().eval4f(0, uv, duvdx, duvdy);
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

struct BadAssetCase {
    char const *name;
    char const *colorSpace;
    void (*write)(std::filesystem::path const &);
};

void writeTwoChannelImage(std::filesystem::path const &imagePath) {
    auto const pixels = std::array{0.25F, 0.75F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1});
}

void writeSinglePixel(std::filesystem::path const &imagePath, OIIO::ImageSpec const &spec) {
    auto const pixels = std::array{0.5F};
    auto const bytes = std::as_bytes(std::span{pixels});
    auto image = OIIO::ImageBuf{spec, OIIO::cspan<std::byte>{bytes.data(), bytes.size()}};
    ASSERT_TRUE(image.write(imagePath.string())) << image.geterror();
}

void writeOffsetDataWindow(std::filesystem::path const &imagePath) {
    auto spec = OIIO::ImageSpec{1, 1, 1, OIIO::TypeDesc::FLOAT};
    spec.x = 1;
    spec.full_x = 1;
    writeSinglePixel(imagePath, spec);
}

void writeInvalidOrientation(std::filesystem::path const &imagePath) {
    auto spec = OIIO::ImageSpec{1, 1, 1, OIIO::TypeDesc::FLOAT};
    spec.attribute("Orientation", 9);
    writeSinglePixel(imagePath, spec);
}

/// A texture whose address mode is border, except on the axis that \c property sets to clamp.
struct AxisAddressModeCase {
    char const *property;
    flux::Vec2f outsideOnClampedAxis;
    flux::Vec2f outsideOnOtherAxis;
};

class EmbreeAxisAddressModeTests : public EmbreeImageTextureTests,
                                   public testing::WithParamInterface<AxisAddressModeCase> {};

class EmbreeBadAssetTests : public EmbreeImageTextureTests,
                            public testing::WithParamInterface<BadAssetCase> {};

class EmbreeImageOrientationTests : public EmbreeImageTextureTests,
                                    public testing::WithParamInterface<ImageOrientationCase> {};
} // namespace

TEST_F(EmbreeImageTextureTests, PreservesStraightAlphaDuringSRGBConversion) {
    auto const imagePath = path("straight-alpha.tif");
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{1.0F, 0.5F, 0.25F, 0.5F};
    flux::test::writeFloatImage(
        imagePath, pixels, {1, 1}, componentNames, flux::ImageComponentType::UNorm8
    );
    build(imagePath, "srgb");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 1.0F, 2.0e-3F);
    EXPECT_NEAR(value.y(), 0.21404114F, 2.0e-3F);
    EXPECT_NEAR(value.z(), 0.05087609F, 2.0e-3F);
    EXPECT_NEAR(value.w(), 128.0F / 255.0F, 2.0e-3F);
}

TEST_P(EmbreeBadAssetTests, BadAssetBecomesMagenta) {
    auto const &parameter = GetParam();
    auto const imagePath = path(std::string{parameter.name} + ".exr");
    if (parameter.write)
        parameter.write(imagePath);
    build(imagePath, parameter.colorSpace);

    EXPECT_EQ(
        context->getObjects<flux::ImageTexture>().front()->getImageAsset()->getExtent(),
        (flux::Vec2u{1, 1})
    );
    expectNear(sample({0.5F, 0.5F}), {1.0F, 0.0F, 1.0F, 1.0F});
}

INSTANTIATE_TEST_SUITE_P(
    Defects, EmbreeBadAssetTests,
    testing::Values(
        BadAssetCase{"MissingFile", "linear", nullptr},
        BadAssetCase{"MissingFileAuto", "auto", nullptr},
        BadAssetCase{"TwoChannelSRGB", "srgb", writeTwoChannelImage},
        BadAssetCase{"OffsetDataWindow", "linear", writeOffsetDataWindow},
        BadAssetCase{"BadOrientation", "linear", writeInvalidOrientation}
    ),
    [](testing::TestParamInfo<BadAssetCase> const &info) { return info.param.name; }
);

TEST_P(EmbreeImageOrientationTests, AppliesImageOrientation) {
    auto const &parameter = GetParam();
    auto const imagePath = path(std::string{parameter.name} + ".exr");
    auto const componentNames = std::array<std::string_view, 1>{"Y"};
    auto const pixels = std::array{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    flux::test::writeFloatImage(
        imagePath, pixels, {3, 2}, componentNames, flux::ImageComponentType::Float32,
        parameter.orientation
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

TEST_F(EmbreeImageTextureTests, AutoColorSpaceSharesTheAssetOfTheColorSpaceItChooses) {
    auto const fixtures = std::filesystem::path{FLUX_TEST_FIXTURES_DIR};
    auto const create = [&](std::filesystem::path const &imagePath, std::string const &colorSpace) {
        kira::Properties props;
        props.set("type", "image");
        props.set("path", imagePath);
        props.set("color_space", colorSpace);
        (void)context->create<flux::Texture>(props);
        return context->getObjects<flux::ImageTexture>().back()->getImageAsset();
    };

    EXPECT_EQ(create(fixtures / "SRGB.ppm", "auto"), create(fixtures / "SRGB.ppm", "srgb"));
    EXPECT_EQ(create(fixtures / "Gray2x2.pgm", "auto"), create(fixtures / "Gray2x2.pgm", "linear"));
}

TEST_F(EmbreeImageTextureTests, EvaluatesPointSamplesOfALinearlyFilteredTexture) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb", "linear");

    expectNear(pool.getImpl().evalPoint4f(0, {0.49F, 0.25F}), {0.0F, 0.0F, 1.0F, 1.0F});
}

TEST_F(EmbreeImageTextureTests, FiltersLinearSRGBValues) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGBFilter2x2.ppm", "srgb", "linear");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.28918776F, 1.5e-3F);
    EXPECT_NEAR(value.y(), 0.28918776F, 1.5e-3F);
    EXPECT_NEAR(value.z(), 0.28918776F, 1.5e-3F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(EmbreeImageTextureTests, FiltersMipLevels) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "linear", "linear");

    constexpr auto expected = 128.0F / 255.0F;
    expectNear(
        sample({0.1F, 0.1F}, {1.0F, 0.0F}, {0.0F, 1.0F}), {expected, expected, expected, 1.0F}
    );
}

TEST_F(EmbreeImageTextureTests, BuildsSRGBMipLevelsInLinearSpace) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGBFilter2x2.ppm", "srgb", "linear");

    auto const value = sample({0.1F, 0.1F}, {1.0F, 0.0F}, {0.0F, 1.0F});
    EXPECT_NEAR(value.x(), 0.28918776F, 1.5e-3F);
    EXPECT_NEAR(value.y(), 0.28918776F, 1.5e-3F);
    EXPECT_NEAR(value.z(), 0.28918776F, 1.5e-3F);
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

TEST_P(EmbreeAxisAddressModeTests, AppliesAddressModeToEachAxis) {
    auto const &parameter = GetParam();
    kira::Properties props;
    props.set("type", "image");
    props.set("path", std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm");
    props.set("filter_mode", std::string{"point"});
    props.set("address_mode", std::string{"border"});
    props.set(parameter.property, std::string{"clamp"});
    build(props);

    auto const inside = sample({0.25F, 0.25F});
    EXPECT_NE(inside, flux::Vec4f{});
    EXPECT_EQ(sample(parameter.outsideOnClampedAxis), inside);
    EXPECT_EQ(sample(parameter.outsideOnOtherAxis), flux::Vec4f{});
}

INSTANTIATE_TEST_SUITE_P(
    Axes, EmbreeAxisAddressModeTests,
    testing::Values(
        AxisAddressModeCase{"address_mode_u", {-0.25F, 0.25F}, {0.25F, -0.25F}},
        AxisAddressModeCase{"address_mode_v", {0.25F, -0.25F}, {-0.25F, 0.25F}}
    ),
    [](testing::TestParamInfo<AxisAddressModeCase> const &info) { return info.param.property; }
);

TEST_P(EmbreeAxisAddressModeTests, AppliesAddressModeToEachAxisOfATransposedImage) {
    auto const &parameter = GetParam();
    auto const imagePath = path("transposed-address-mode.exr");
    auto const componentNames = std::array<std::string_view, 1>{"Y"};
    auto const pixels = std::array{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    flux::test::writeFloatImage(
        imagePath, pixels, {3, 2}, componentNames, flux::ImageComponentType::Float32, 6
    );
    kira::Properties props;
    props.set("type", "image");
    props.set("path", imagePath);
    props.set("filter_mode", std::string{"point"});
    props.set("address_mode", std::string{"border"});
    props.set(parameter.property, std::string{"clamp"});
    build(props);

    auto const inside = sample({0.25F, 0.25F}).x();
    EXPECT_NE(inside, 0.0F);
    EXPECT_EQ(sample(parameter.outsideOnClampedAxis).x(), inside);
    EXPECT_EQ(sample(parameter.outsideOnOtherAxis).x(), 0.0F);
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

TEST_F(EmbreeImageTextureTests, AppliesScaleAndBiasAfterComponentMapping) {
    auto const imagePath = path("scale-bias.exr");
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1}, componentNames);
    kira::Properties props;
    props.set("type", "image");
    props.set("path", imagePath);
    props.set("filter_mode", std::string{"point"});
    props.set("component_mapping", std::string{"zyx1"});
    props.set("scale", flux::Vec4f{2.0F, 4.0F, 8.0F, 0.5F});
    props.set("bias", flux::Vec4f{1.0F, 0.25F, -1.0F, 0.25F});
    build(props);

    EXPECT_EQ(sample({0.5F, 0.5F}), (flux::Vec4f{2.5F, 2.25F, 1.0F, 0.75F}));
}

TEST_F(EmbreeImageTextureTests, FallbackReplacesComponentMappingOfAnImageThatFailsToLoad) {
    kira::Properties props;
    props.set("type", "image");
    props.set("path", path("MissingFallback.exr"));
    props.set("component_mapping", std::string{"0000"});
    props.set("scale", flux::Vec4f{2.0F, 4.0F, 8.0F, 0.5F});
    props.set("bias", flux::Vec4f{1.0F, 0.25F, -1.0F, 0.25F});
    props.set("fallback", flux::Vec4f{0.5F, 0.25F, 0.125F, 0.75F});
    build(props);

    EXPECT_EQ(sample({0.5F, 0.5F}), (flux::Vec4f{2.0F, 1.25F, 0.0F, 0.625F}));
}

TEST_F(EmbreeImageTextureTests, FallbackLeavesALoadedImage) {
    auto const imagePath = path("fallback-loaded.exr");
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1}, componentNames);
    kira::Properties props;
    props.set("type", "image");
    props.set("path", imagePath);
    props.set("filter_mode", std::string{"point"});
    props.set("fallback", flux::Vec4f{9.0F, 9.0F, 9.0F, 9.0F});
    build(props);

    EXPECT_EQ(sample({0.5F, 0.5F}), (flux::Vec4f{0.25F, 0.5F, 0.75F, 1.0F}));
}
