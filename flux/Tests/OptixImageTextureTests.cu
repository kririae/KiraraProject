#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "TestUtils.h"
#include "flux/IO/ImageIO.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixImagePool.h"
#include "flux/Optix/OptixImageTexture.cuh"
#include "flux/Optix/OptixTextureObject.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

namespace {
struct SampleImageTexture {
    flux::OptixImageTextureTable table;
    flux::Vec2f const *uvs;
    flux::Vec4f *samples;
    flux::Vec2f duvdx;
    flux::Vec2f duvdy;
    bool point;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept {
        auto const &texture = table.getTexture(0);
        auto const value =
            point ? texture.samplePoint(uvs[index]) : texture.sample(uvs[index], duvdx, duvdy);
        samples[index] = texture.componentMapping.apply(value);
    }
};

class OptixImageTextureTests : public testing::Test {
protected:
    void SetUp() override {
        if (!flux::test::hasCudaMemoryPoolSupport())
            GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";
    }

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

        // Place the one texture at index zero, as the texture stage of OptixContext does.
        auto const texture = context->getObjects<flux::ImageTexture>().front();
        auto const assets = std::array{texture->getImageAsset()};
        pool.add(assets);
        auto const image = pool.getImpl(texture->getImageAsset()->getId());
        filtered =
            flux::createTextureObject(image, texture->getAddressMode(), texture->getFilterMode());
        point = flux::createTextureObject(
            image, texture->getAddressMode(), flux::ImageTextureFilterMode::Point
        );
        auto const entry = flux::OptixImageTexture{
            .texture = filtered.get(),
            .pointTexture = point.get(),
            .componentMapping = texture->getComponentMapping(),
            .componentCount = image.componentCount,
        };
        textures.copyFromHostAndWait({&entry, 1});
    }

    [[nodiscard]] std::vector<flux::Vec4f> sample(
        std::span<flux::Vec2f const> uvs, bool point = false, flux::Vec2f duvdx = {},
        flux::Vec2f duvdy = {}
    ) const {
        flux::DeviceBuffer<flux::Vec2f> deviceUVs(cudaStreamPerThread);
        flux::DeviceBuffer<flux::Vec4f> deviceSamples(cudaStreamPerThread);
        deviceUVs.copyFromHostAndWait(uvs);
        deviceSamples.resize(uvs.size());
        flux::launchLinearKernel(
            uvs.size(),
            SampleImageTexture{
                .table = {.textures = textures.data()},
                .uvs = deviceUVs.data(),
                .samples = deviceSamples.data(),
                .duvdx = duvdx,
                .duvdy = duvdy,
                .point = point,
            },
            cudaStreamPerThread
        );

        auto result = std::vector<flux::Vec4f>(uvs.size());
        deviceSamples.copyToHost(result);
        return result;
    }

    [[nodiscard]] flux::Vec4f sample(
        flux::Vec2f uv, bool point = false, flux::Vec2f duvdx = {}, flux::Vec2f duvdy = {}
    ) const {
        auto const uvs = std::array{uv};
        return sample(uvs, point, duvdx, duvdy).front();
    }

    flux::Ref<flux::Context> context = flux::Context::create();
    flux::DeletionQueue deletionQueue{cudaStreamPerThread};
    flux::OptixImagePool pool{deletionQueue};
    flux::CudaTextureObject filtered;
    flux::CudaTextureObject point;
    flux::DeviceBuffer<flux::OptixImageTexture> textures{cudaStreamPerThread};
};

struct ImageStorageCase {
    char const *name;
    flux::ImageComponentType componentType;
    std::uint8_t componentCount;
    char const *extension;
    float tolerance;
};

class OptixImageStorageTests : public OptixImageTextureTests,
                               public testing::WithParamInterface<ImageStorageCase> {};

class OptixSRGBStorageTests : public OptixImageTextureTests,
                              public testing::WithParamInterface<ImageStorageCase> {};

struct ImageOrientationCase {
    char const *name;
    int orientation;
    flux::Vec2u extent;
    std::array<float, 6> bottomUp;
};

class OptixImageOrientationTests : public OptixImageTextureTests,
                                   public testing::WithParamInterface<ImageOrientationCase> {};
} // namespace

TEST_P(OptixImageStorageTests, SamplesStoredComponents) {
    auto const &parameter = GetParam();
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    auto const imagePath = path(std::string{parameter.name} + parameter.extension);
    flux::test::writeFloatImage(
        imagePath, {pixels.data(), parameter.componentCount}, {1, 1},
        {componentNames.data(), parameter.componentCount}, parameter.componentType
    );
    build(imagePath);

    auto const &asset = context->getObjects<flux::ImageTexture>().front()->getImageAsset();
    ASSERT_EQ(asset->getComponentType(), parameter.componentType);
    ASSERT_EQ(asset->getComponentCount(), parameter.componentCount);

    auto const value = sample({0.5F, 0.5F});
    auto expected = flux::Vec4f{pixels[0], pixels[0], pixels[0], 1.0F};
    if (parameter.componentCount == 2)
        expected = flux::Vec4f{pixels[0], pixels[0], pixels[0], pixels[1]};
    else if (parameter.componentCount == 4)
        expected = flux::Vec4f{pixels[0], pixels[1], pixels[2], pixels[3]};

    EXPECT_NEAR(value.x(), expected.x(), parameter.tolerance);
    EXPECT_NEAR(value.y(), expected.y(), parameter.tolerance);
    EXPECT_NEAR(value.z(), expected.z(), parameter.tolerance);
    EXPECT_NEAR(value.w(), expected.w(), parameter.tolerance);
}

INSTANTIATE_TEST_SUITE_P(
    ComponentLayouts, OptixImageStorageTests,
    testing::Values(
        ImageStorageCase{"UNorm8_1", flux::ImageComponentType::UNorm8, 1, ".tif", 2.0e-3F},
        ImageStorageCase{"UNorm8_2", flux::ImageComponentType::UNorm8, 2, ".tif", 2.0e-3F},
        ImageStorageCase{"UNorm8_4", flux::ImageComponentType::UNorm8, 4, ".tif", 2.0e-3F},
        ImageStorageCase{"Float16_1", flux::ImageComponentType::Float16, 1, ".exr", 1.0e-3F},
        ImageStorageCase{"Float16_2", flux::ImageComponentType::Float16, 2, ".exr", 1.0e-3F},
        ImageStorageCase{"Float16_4", flux::ImageComponentType::Float16, 4, ".exr", 1.0e-3F},
        ImageStorageCase{"Float32_1", flux::ImageComponentType::Float32, 1, ".exr", 1.0e-6F},
        ImageStorageCase{"Float32_2", flux::ImageComponentType::Float32, 2, ".exr", 1.0e-6F},
        ImageStorageCase{"Float32_4", flux::ImageComponentType::Float32, 4, ".exr", 1.0e-6F}
    ),
    [](testing::TestParamInfo<ImageStorageCase> const &info) { return info.param.name; }
);

TEST_P(OptixImageOrientationTests, AppliesImageOrientation) {
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
    Orientations, OptixImageOrientationTests,
    testing::Values(
        ImageOrientationCase{"Identity", 1, {3, 2}, {4, 5, 6, 1, 2, 3}},
        ImageOrientationCase{"MirrorVertical", 4, {3, 2}, {1, 2, 3, 4, 5, 6}},
        ImageOrientationCase{"Transpose", 5, {2, 3}, {4, 1, 5, 2, 6, 3}},
        ImageOrientationCase{"Rotate270", 8, {2, 3}, {1, 4, 2, 5, 3, 6}}
    ),
    [](testing::TestParamInfo<ImageOrientationCase> const &info) { return info.param.name; }
);

TEST_F(OptixImageTextureTests, FiltersLinearSRGBValues) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGBFilter2x2.ppm", "srgb", "linear");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.28918776F, 2.0e-4F);
    EXPECT_NEAR(value.y(), 0.28918776F, 2.0e-4F);
    EXPECT_NEAR(value.z(), 0.28918776F, 2.0e-4F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(OptixImageTextureTests, FiltersMipLevels) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "linear", "linear");

    auto const value = sample({0.1F, 0.1F}, false, {1.0F, 0.0F}, {0.0F, 1.0F});
    constexpr auto expected = 128.0F / 255.0F;
    EXPECT_NEAR(value.x(), expected, 1.0e-5F);
    EXPECT_NEAR(value.y(), expected, 1.0e-5F);
    EXPECT_NEAR(value.z(), expected, 1.0e-5F);
}

TEST_F(OptixImageTextureTests, FiltersOddSizedMipLevels) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture3x3.ppm", "linear", "linear");

    auto const value = sample({0.5F, 0.5F}, false, {1.0F, 0.0F}, {0.0F, 1.0F});
    EXPECT_FLOAT_EQ(value.x(), 1.0F);
    EXPECT_FLOAT_EQ(value.y(), 1.0F);
    EXPECT_FLOAT_EQ(value.z(), 1.0F);
}

TEST_F(OptixImageTextureTests, BuildsSRGBMipLevelsInLinearSpace) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGBFilter2x2.ppm", "srgb", "linear");

    auto const value = sample({0.1F, 0.1F}, false, {1.0F, 0.0F}, {0.0F, 1.0F});
    EXPECT_NEAR(value.x(), 0.28918776F, 2.5e-3F);
    EXPECT_NEAR(value.y(), 0.28918776F, 2.5e-3F);
    EXPECT_NEAR(value.z(), 0.28918776F, 2.5e-3F);
}

TEST_F(OptixImageTextureTests, KeepsPointFilteredView) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb", "linear");

    EXPECT_EQ(sample({0.49F, 0.25F}, true), (flux::Vec4f{0.0F, 0.0F, 1.0F, 1.0F}));
}

TEST_F(OptixImageTextureTests, MoveKeepsTheHandleAndEmptiesTheSource) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm");
    auto const handle = filtered.get();
    ASSERT_NE(handle, cudaTextureObject_t{});
    ASSERT_NE(point.get(), cudaTextureObject_t{});

    flux::CudaTextureObject moved(std::move(filtered));
    EXPECT_EQ(moved.get(), handle);
    EXPECT_EQ(filtered.get(), cudaTextureObject_t{});

    point = std::move(moved);
    EXPECT_EQ(point.get(), handle);
    EXPECT_EQ(moved.get(), cudaTextureObject_t{});
}

TEST_F(OptixImageTextureTests, WrapsCoordinates) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb");

    EXPECT_EQ(sample({1.25F, 0.25F}), sample({0.25F, 0.25F}));
}

TEST_F(OptixImageTextureTests, FiltersAcrossBorder) {
    build(
        std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "linear", "linear",
        "border"
    );

    EXPECT_EQ(sample({-0.25F, 0.25F}), flux::Vec4f{});
    auto const edge = sample({0.0F, 0.25F});
    EXPECT_FLOAT_EQ(edge.x(), 0.0F);
    EXPECT_FLOAT_EQ(edge.y(), 0.0F);
    EXPECT_NEAR(edge.z(), 0.5F, 1.0e-4F);
    EXPECT_NEAR(edge.w(), 0.5F, 1.0e-4F);
}

TEST_F(OptixImageTextureTests, AppliesComponentMappingAfterBorderFiltering) {
    build(
        std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "linear", "linear",
        "border", "1x0w"
    );

    EXPECT_EQ(sample({-0.25F, 0.25F}), (flux::Vec4f{1.0F, 0.0F, 0.0F, 0.0F}));
    auto const edge = sample({0.0F, 0.25F});
    EXPECT_FLOAT_EQ(edge.x(), 1.0F);
    EXPECT_FLOAT_EQ(edge.y(), 0.0F);
    EXPECT_FLOAT_EQ(edge.z(), 0.0F);
    EXPECT_NEAR(edge.w(), 0.5F, 1.0e-4F);
}

TEST_F(OptixImageTextureTests, DecodesUNorm8SRGB) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGB.ppm", "srgb");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.03310477F, 2.5e-4F);
    EXPECT_NEAR(value.y(), 0.13286832F, 2.5e-4F);
    EXPECT_NEAR(value.z(), 0.31854678F, 2.5e-4F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(OptixImageTextureTests, PreservesStraightAlphaDuringSRGBConversion) {
    auto const imagePath = path("optix-straight-alpha.tif");
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

TEST_P(OptixSRGBStorageTests, ConvertsComponentsBeforeUpload) {
    auto const &parameter = GetParam();
    auto const imagePath = path(std::string{parameter.name} + parameter.extension);
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.5F, 0.25F, 0.75F, 0.4F};
    flux::test::writeFloatImage(imagePath, pixels, {1, 1}, componentNames, parameter.componentType);
    build(imagePath, "srgb");

    EXPECT_EQ(
        context->getObjects<flux::ImageTexture>().front()->getImageAsset()->getComponentType(),
        parameter.componentType
    );
    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.21404114F, parameter.tolerance);
    EXPECT_NEAR(value.y(), 0.05087609F, parameter.tolerance);
    EXPECT_NEAR(value.z(), 0.52252155F, parameter.tolerance);
    EXPECT_NEAR(value.w(), 0.4F, parameter.tolerance);
}

INSTANTIATE_TEST_SUITE_P(
    FloatingPointStorage, OptixSRGBStorageTests,
    testing::Values(
        ImageStorageCase{"Float16SRGB", flux::ImageComponentType::Float16, 4, ".exr", 1.0e-3F},
        ImageStorageCase{"Float32SRGB", flux::ImageComponentType::Float32, 4, ".exr", 2.0e-5F}
    ),
    [](testing::TestParamInfo<ImageStorageCase> const &info) { return info.param.name; }
);

namespace {
[[nodiscard]] flux::Ref<flux::ImageAsset const>
createAsset(flux::Context &context, std::string_view filename) {
    kira::Properties props;
    props.set("type", "image");
    props.set("path", std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / filename);
    (void)context.create<flux::Texture>(props);
    auto const textures = context.getObjects<flux::ImageTexture>();
    return textures.back()->getImageAsset();
}
} // namespace

TEST(OptixImagePoolTests, AddsAndRemovesImagesInBatches) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    auto const assets = std::array{
        createAsset(*context, "Texture2x2.ppm"),
        createAsset(*context, "Texture3x3.ppm"),
    };
    auto const kept = assets[0]->getId();
    auto const dropped = assets[1]->getId();

    flux::DeletionQueue deletionQueue{cudaStreamPerThread};
    flux::OptixImagePool pool{deletionQueue};
    pool.add(assets);
    EXPECT_EQ(pool.getIds().size(), 2U);

    // The second ID is absent once the first removes the image.
    auto const removed = std::array{dropped, dropped};
    pool.remove(removed);
    EXPECT_TRUE(pool.contains(kept));
    EXPECT_FALSE(pool.contains(dropped));
    EXPECT_THROW((void)pool.getImpl(dropped), std::out_of_range);
}
