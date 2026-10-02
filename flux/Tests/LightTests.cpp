#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>

#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/EnvMapLightImpl.h"

namespace {
struct UVTextureEvaluator {
    [[nodiscard]] static flux::Vec4f
    eval4f(std::uint32_t, flux::Vec2f uv, flux::Vec2f const &, flux::Vec2f const &) noexcept {
        return {uv.x(), uv.y(), 0.0F, 1.0F};
    }
};

struct GradientTextureEvaluator {
    [[nodiscard]] static flux::Vec4f eval4f(
        std::uint32_t, flux::Vec2f, flux::Vec2f const &duvdx, flux::Vec2f const &duvdy
    ) noexcept {
        return {duvdx.x(), duvdy.y(), 0.0F, 1.0F};
    }
};
} // namespace

TEST(LightTests, ActivatesOnlyTheEnvironmentMapTheHostSets) {
    auto context = flux::Context::create();

    EXPECT_FALSE(context->getActiveEnvMap());
    auto first = context->create<flux::EnvMapLight>(kira::Properties{});
    auto second = context->create<flux::EnvMapLight>(kira::Properties{});
    EXPECT_FALSE(context->getActiveEnvMap());

    context->setActiveEnvMap(second);
    EXPECT_EQ(context->getActiveEnvMap(), second);
}

TEST(LightTests, ClampsEnvironmentMapScale) {
    auto context = flux::Context::create();
    auto light = context->create<flux::EnvMapLight>(kira::Properties{});

    light->setScale({-1.0F, 2.0F, 3.0F});

    EXPECT_EQ(light->getScale(), (flux::Spectrum{0.0F, 2.0F, 3.0F}));
}

TEST(LightTests, MapsEnvironmentCoordinatesToImageCoordinates) {
    auto const light = flux::EnvMapLight::Impl{
        .texture =
            {
                .type = flux::TextureType::Image,
                .storage = {.image = {.imageTextureIndex = 0}},
            },
        .scale = {1.0F, 1.0F, 1.0F},
        .worldToEnv = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F},
    };

    EXPECT_EQ(
        light.eval<UVTextureEvaluator>({1.0F, 0.0F, 0.0F}), (flux::Spectrum{0.0F, 0.5F, 0.0F})
    );
    EXPECT_EQ(
        light.eval<UVTextureEvaluator>({0.0F, 1.0F, 0.0F}), (flux::Spectrum{0.25F, 0.5F, 0.0F})
    );
    EXPECT_EQ(
        light.eval<UVTextureEvaluator>({0.0F, 0.0F, 1.0F}), (flux::Spectrum{0.0F, 1.0F, 0.0F})
    );
}

TEST(LightTests, MapsEnvironmentFootprintsToImageGradients) {
    auto const light = flux::EnvMapLight::Impl{
        .texture =
            {
                .type = flux::TextureType::Image,
                .storage = {.image = {.imageTextureIndex = 0}},
            },
        .scale = {1.0F, 1.0F, 1.0F},
        .worldToEnv = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F},
    };
    constexpr auto angle = 0.2F;

    auto const value = light.eval<GradientTextureEvaluator>({1.0F, 0.0F, 0.0F}, angle);

    EXPECT_NEAR(value.x(), angle / (2.0F * std::numbers::pi_v<float>), 1.0e-7F);
    EXPECT_NEAR(value.y(), -angle * std::numbers::inv_pi_v<float>, 1.0e-7F);

    auto const pole = light.eval<GradientTextureEvaluator>({0.0F, 0.0F, 1.0F}, angle);
    EXPECT_FLOAT_EQ(pole.x(), 1.0F);
}

TEST(LightTests, UsesFluxWorldAxesForEnvironmentMaps) {
    auto context = flux::Context::create();
    auto light = context->create<flux::EnvMapLight>(kira::Properties{})->getImpl({});
    auto const ctx = flux::LightSamplingContext{};

    auto const atZero = light.sampleDirect<UVTextureEvaluator>(ctx, {0.0F, 0.5F});
    EXPECT_NEAR(atZero.wi.x(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(atZero.wi.y(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(atZero.wi.z(), 1.0F, 1.0e-6F);

    auto const atQuarter = light.sampleDirect<UVTextureEvaluator>(ctx, {0.25F, 0.5F});
    EXPECT_NEAR(atQuarter.wi.x(), -1.0F, 1.0e-6F);
    EXPECT_NEAR(atQuarter.wi.y(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(atQuarter.wi.z(), 0.0F, 1.0e-6F);
}

TEST(LightTests, AppliesEnvironmentMapRotationInDegrees) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("rotation", flux::Vec3f{0.0F, 90.0F, 0.0F});
    auto light = context->create<flux::EnvMapLight>(props)->getImpl({});

    auto const sample = light.sampleDirect<UVTextureEvaluator>({}, {0.0F, 0.5F});
    EXPECT_NEAR(sample.wi.x(), 1.0F, 1.0e-6F);
    EXPECT_NEAR(sample.wi.y(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(sample.wi.z(), 0.0F, 1.0e-6F);
}

TEST(LightTests, AppliesEnvironmentMapRotationInXYZOrder) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("rotation", flux::Vec3f{90.0F, 90.0F, 90.0F});
    auto light = context->create<flux::EnvMapLight>(props)->getImpl({});

    auto const sample = light.sampleDirect<UVTextureEvaluator>({}, {0.25F, 0.5F});
    EXPECT_NEAR(sample.wi.x(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(sample.wi.y(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(sample.wi.z(), 1.0F, 1.0e-6F);
}

TEST(LightTests, KeepsImageEnvironmentPDFConsistent) {
    auto const condCDF = std::array{0.25F, 1.0F, 1.0F / 3.0F, 1.0F};
    auto const rowCDF = std::array{0.4F, 1.0F};
    auto const light = flux::EnvMapLight::Impl{
        .texture =
            {
                .type = flux::TextureType::Image,
                .storage = {.image = {.imageTextureIndex = 0}},
            },
        .distribution =
            {
                .condCDF = condCDF.data(),
                .rowCDF = rowCDF.data(),
                .extent = {2, 2},
            },
        .scale = {1.0F, 1.0F, 1.0F},
        .worldToEnv = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F},
    };

    auto const sample = light.sampleDirect<UVTextureEvaluator>({}, {0.2F, 0.7F});
    auto const expectedPdf =
        0.8F / (2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * std::sqrt(0.5F));
    EXPECT_NEAR(sample.pdf, expectedPdf, 1.0e-6F);
    EXPECT_NEAR(light.pdf(sample.wi), expectedPdf, 1.0e-6F);

    float fusedPdf;
    auto const value = light.evalAndPdf<UVTextureEvaluator>(sample.wi, fusedPdf);
    EXPECT_NEAR(fusedPdf, expectedPdf, 1.0e-6F);
    EXPECT_NEAR(value.x(), 0.3F, 1.0e-6F);
    EXPECT_NEAR(value.y(), 0.25F, 1.0e-6F);
    EXPECT_EQ(value.z(), 0.0F);
}
