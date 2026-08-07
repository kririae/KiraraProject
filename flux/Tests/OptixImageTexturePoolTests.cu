#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "TestUtils.h"
#include "flux/IO/ImageIO.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixImageTexturePool.cuh"
#include "flux/Scene/Context.h"
#include "flux/Shading/Texture.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

namespace {
struct SampleImageTexture {
    flux::OptixImageTexturePool::Impl pool;
    flux::Vec2f const *uvs;
    flux::Vec4f *samples;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept {
        auto const &texture = pool.get(0);
        samples[index] = texture.componentMapping.apply(texture.sample(uvs[index]));
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

    [[nodiscard]] std::vector<flux::Vec4f> sample(std::span<flux::Vec2f const> uvs) const {
        flux::DeviceBuffer<flux::Vec2f> deviceUVs(cudaStreamPerThread);
        flux::DeviceBuffer<flux::Vec4f> deviceSamples(cudaStreamPerThread);
        deviceUVs.copyFromHost(uvs);
        deviceSamples.resize(uvs.size());
        flux::launchLinearKernel(
            uvs.size(),
            SampleImageTexture{
                .pool = pool.getImpl(),
                .uvs = deviceUVs.data(),
                .samples = deviceSamples.data(),
            },
            cudaStreamPerThread
        );

        auto result = std::vector<flux::Vec4f>(uvs.size());
        deviceSamples.copyToHost(result);
        flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
        return result;
    }

    [[nodiscard]] flux::Vec4f sample(flux::Vec2f uv) const {
        auto const uvs = std::array{uv};
        return sample(uvs).front();
    }

    flux::Ref<flux::Context> context = flux::Context::create();
    flux::OptixImageTexturePool pool{cudaStreamPerThread};
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
} // namespace

TEST_P(OptixImageStorageTests, SamplesStoredComponents) {
    auto const &parameter = GetParam();
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.25F, 0.5F, 0.75F, 1.0F};
    auto const imagePath = path(std::string{parameter.name} + parameter.extension);
    flux::writeImage(
        imagePath,
        {
            .pixels = std::as_bytes(std::span{pixels.data(), parameter.componentCount}),
            .extent = {1, 1},
            .componentType = flux::ImageComponentType::Float32,
            .componentCount = parameter.componentCount,
        },
        {
            .outputComponentType = parameter.componentType,
            .componentNames = {componentNames.data(), parameter.componentCount},
        }
    );
    build(imagePath);

    auto const &asset = context->getObjects<flux::ImageTexture>().front()->getImageAsset();
    ASSERT_EQ(asset->getComponentType(), parameter.componentType);
    ASSERT_EQ(asset->getComponentCount(), parameter.componentCount);

    auto const value = sample({0.5F, 0.5F});
    auto expected = flux::Vec4f{pixels[0], pixels[0], pixels[0], 1.0F};
    if (parameter.componentCount == 2)
        expected = flux::Vec4f{pixels[0], pixels[1], 0.0F, 1.0F};
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

TEST_F(OptixImageTextureTests, SamplesBottomUpCoordinates) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb");
    auto const uvs = std::array{
        flux::Vec2f{0.25F, 0.25F},
        flux::Vec2f{0.75F, 0.25F},
        flux::Vec2f{0.25F, 0.75F},
        flux::Vec2f{0.75F, 0.75F},
    };
    auto const values = sample(uvs);

    EXPECT_EQ(values[0], (flux::Vec4f{0.0F, 0.0F, 1.0F, 1.0F}));
    EXPECT_EQ(values[1], (flux::Vec4f{1.0F, 1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(values[2], (flux::Vec4f{1.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(values[3], (flux::Vec4f{0.0F, 1.0F, 0.0F, 1.0F}));
}

TEST_F(OptixImageTextureTests, FiltersLinearSRGBValues) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb", "linear");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.5F, 1.0e-4F);
    EXPECT_NEAR(value.y(), 0.5F, 1.0e-4F);
    EXPECT_NEAR(value.z(), 0.5F, 1.0e-4F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(OptixImageTextureTests, WrapsCoordinates) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "Texture2x2.ppm", "srgb");

    EXPECT_EQ(sample({1.25F, 0.25F}), sample({0.25F, 0.25F}));
}

TEST_F(OptixImageTextureTests, DecodesUNorm8SRGB) {
    build(std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "SRGB.ppm", "srgb");

    auto const value = sample({0.5F, 0.5F});
    EXPECT_NEAR(value.x(), 0.03310477F, 2.5e-4F);
    EXPECT_NEAR(value.y(), 0.13286832F, 2.5e-4F);
    EXPECT_NEAR(value.z(), 0.31854678F, 2.5e-4F);
    EXPECT_FLOAT_EQ(value.w(), 1.0F);
}

TEST_F(OptixImageTextureTests, ConvertsFloatSRGBBeforeUpload) {
    auto const imagePath = path("float-srgb.exr");
    auto const componentNames = std::array<std::string_view, 4>{"R", "G", "B", "A"};
    auto const pixels = std::array{0.5F, 0.25F, 0.75F, 0.4F};
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
