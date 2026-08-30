#include <gtest/gtest.h>

#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/EnvMapLightImpl.h"

namespace {
struct UVTextureEvaluator {
    [[nodiscard]] static flux::Vec4f eval4f(std::uint32_t, flux::Vec2f uv) noexcept {
        return {uv.x(), uv.y(), 0.0F, 1.0F};
    }
};
} // namespace

TEST(LightTests, KeepsFirstEnvironmentMapActive) {
    auto context = flux::Context::create();

    EXPECT_FALSE(context->getActiveEnvMap());
    auto first = context->create<flux::EnvMapLight>();
    EXPECT_EQ(context->getActiveEnvMap(), first);

    (void)context->create<flux::EnvMapLight>();
    EXPECT_EQ(context->getActiveEnvMap(), first);
}

TEST(LightTests, ClampsEnvironmentMapScale) {
    auto context = flux::Context::create();
    auto light = context->create<flux::EnvMapLight>();

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

TEST(LightTests, UsesFluxWorldAxesForEnvironmentMaps) {
    auto context = flux::Context::create();
    auto light = context->create<flux::EnvMapLight>()->getImpl({});
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
    props.set("rotation", flux::Vec3f{90.0F, 90.0F, 0.0F});
    auto light = context->create<flux::EnvMapLight>(props)->getImpl({});

    auto const sample = light.sampleDirect<UVTextureEvaluator>({}, {0.0F, 0.5F});
    EXPECT_NEAR(sample.wi.x(), 1.0F, 1.0e-6F);
    EXPECT_NEAR(sample.wi.y(), 0.0F, 1.0e-6F);
    EXPECT_NEAR(sample.wi.z(), 0.0F, 1.0e-6F);
}
