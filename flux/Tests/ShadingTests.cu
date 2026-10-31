#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/BSDFImpl.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Frame.h"
#include "kira/Anyhow.h"

namespace {
struct TestImageTextureEvaluator {
    [[nodiscard]] KIRA_HOST_DEVICE static flux::Vec4f
    eval4f(std::uint32_t index, flux::Vec2f uv, flux::Vec2f const &, flux::Vec2f const &) noexcept {
        return {static_cast<float>(index) + uv.x(), uv.y(), 3.0F, 4.0F};
    }
};

[[nodiscard]] flux::Texture::Impl constantTexture(flux::Spectrum const &value) {
    return {
        .type = flux::TextureType::Constant,
        .storage = {.constant = flux::ConstantTexture::Impl{value}},
    };
}

struct SpecializedTexture : flux::TextureMixin<SpecializedTexture> {
    [[nodiscard]] KIRA_HOST_DEVICE float eval1f_(flux::TextureEvalContext const &) const noexcept {
        return 1.0F;
    }

    [[nodiscard]] KIRA_HOST_DEVICE flux::Vec2f
    eval2f_(flux::TextureEvalContext const &) const noexcept {
        return {9.0F, 10.0F};
    }

    [[nodiscard]] KIRA_HOST_DEVICE flux::Vec3f
    eval3f_(flux::TextureEvalContext const &) const noexcept {
        return {2.0F, 3.0F, 4.0F};
    }

    [[nodiscard]] KIRA_HOST_DEVICE flux::Vec4f
    eval4f_(flux::TextureEvalContext const &) const noexcept {
        return {5.0F, 6.0F, 7.0F, 8.0F};
    }
};

struct RGBTexture : flux::TextureMixin<RGBTexture> {
    [[nodiscard]] KIRA_HOST_DEVICE flux::Vec3f
    eval3f_(flux::TextureEvalContext const &) const noexcept {
        return {2.0F, 3.0F, 4.0F};
    }

    [[nodiscard]] KIRA_HOST_DEVICE flux::Vec4f
    eval4f_(flux::TextureEvalContext const &) const noexcept {
        return {5.0F, 6.0F, 7.0F, 8.0F};
    }
};

struct EvaluateDiffuse {
    flux::DiffuseBSDF::Impl bsdf;
    flux::BSDFEvaluation *evaluation;
    flux::BSDFSample *sample;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        auto const result = bsdf.execute<TestImageTextureEvaluator>(
            {}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, true, 0.5F, {0.5F, 0.5F}
        );
        *evaluation = result.evaluation;
        *sample = result.sample;
    }
};

struct EvaluatePrincipled {
    flux::PrincipledBSDF::Impl bsdf;
    flux::BSDFResult *result;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        *result = bsdf.execute<TestImageTextureEvaluator>(
            {}, flux::Vec3f{0.3F, -0.2F, 1.0F}.normalize(),
            flux::Vec3f{0.4F, 0.1F, 0.911043F}.normalize(), true, 0.45F, {0.3F, 0.7F}
        );
    }
};

[[nodiscard]] kira::Properties principledProperties() {
    kira::Properties props;
    props.set("type", "principled");
    props.set("base_color", flux::Spectrum{0.7F, 0.2F, 0.1F});
    props.set("roughness", 0.35F);
    props.set("metallic", 0.2F);
    props.set("spec_trans", 0.3F);
    props.set("spec_tint", 0.4F);
    props.set("sheen", 0.25F);
    props.set("sheen_tint", 0.6F);
    props.set("flatness", 0.1F);
    props.set("clearcoat", 0.5F);
    props.set("clearcoat_roughness", 0.15F);
    props.set("eta", 1.45F);
    return props;
}
} // namespace

TEST(ShadingTests, BuildsTextureEvaluationInterface) {
    auto const constant = flux::ConstantTexture::Impl{flux::Spectrum{0.25F, 0.5F, 1.0F}};
    EXPECT_EQ(constant.eval1f({}), 0.25F);
    EXPECT_EQ(constant.eval2f({}), (flux::Vec2f{0.25F, 0.5F}));
    EXPECT_EQ(constant.eval3f({}), (flux::Vec3f{0.25F, 0.5F, 1.0F}));
    EXPECT_EQ(constant.eval4f({}), (flux::Vec4f{0.25F, 0.5F, 1.0F, 1.0F}));

    auto const specialized = SpecializedTexture{};
    EXPECT_EQ(specialized.eval1f({}), 1.0F);
    EXPECT_EQ(specialized.eval2f({}), (flux::Vec2f{9.0F, 10.0F}));
    EXPECT_EQ(specialized.eval3f({}), (flux::Vec3f{2.0F, 3.0F, 4.0F}));
    EXPECT_EQ(specialized.eval4f({}), (flux::Vec4f{5.0F, 6.0F, 7.0F, 8.0F}));

    auto const rgb = RGBTexture{};
    EXPECT_EQ(rgb.eval1f({}), 2.0F);
    EXPECT_EQ(rgb.eval2f({}), (flux::Vec2f{2.0F, 3.0F}));
    EXPECT_EQ(rgb.eval3f({}), (flux::Vec3f{2.0F, 3.0F, 4.0F}));
    EXPECT_EQ(rgb.eval4f({}), (flux::Vec4f{5.0F, 6.0F, 7.0F, 8.0F}));
}

TEST(ShadingTests, BuildsAnOrthonormalFrame) {
    auto const normal = flux::Vec3f{0.2F, -0.3F, 0.9327379F}.normalize();
    flux::Frame const frame(normal);
    auto const value = flux::Vec3f{1.0F, 2.0F, 3.0F};
    auto const local = frame.toLocal(value);
    auto const roundTrip = frame.toWorld(local);

    EXPECT_NEAR(roundTrip.x(), value.x(), 1.0e-5F);
    EXPECT_NEAR(roundTrip.y(), value.y(), 1.0e-5F);
    EXPECT_NEAR(roundTrip.z(), value.z(), 1.0e-5F);
    EXPECT_NEAR(frame.toLocal(normal).z(), 1.0F, 1.0e-5F);
}

TEST(ShadingTests, KeepsShortVisibilityRaysPointedAtTheirTarget) {
    auto const surface = flux::SurfaceInteraction{
        .position = {0.0F, 0.0F, 0.0F},
        .geometricNormal = {0.0F, 0.0F, 1.0F},
    };

    auto const ray = surface.spawnRayTo({0.0F, 0.0F, 1.0e-7F});

    EXPECT_GT(ray.origin.z(), 0.0F);
    EXPECT_LT(ray.origin.z(), 1.0e-7F);
    EXPECT_EQ(ray.direction, (flux::Vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_GT(ray.maxDistance, 0.0F);
}

TEST(ShadingTests, StopsVisibilityRaysBeforeTheEmitterAfterOffsetting) {
    auto const surface = flux::SurfaceInteraction{
        .position = {0.0F, 1.2F, 0.0F},
        .geometricNormal = {0.0F, 1.0F, 0.0F},
    };
    auto const target = flux::Vec3f{0.1F, 2.0F, 0.2F};
    auto const d = target - surface.position;
    auto const ray = surface.spawnRayTo(target);
    auto const emitterDistance = (target.y() - ray.origin.y()) / ray.direction.y();
    EXPECT_LT(ray.maxDistance, emitterDistance);
    EXPECT_NEAR((ray.origin + ray.direction * emitterDistance - target).norm(), 0.0F, 1.0e-6F);

    auto const infinite = surface.spawnRay(d.normalize());
    EXPECT_EQ(infinite.maxDistance, std::numeric_limits<float>::max());
    EXPECT_EQ(infinite.direction, d.normalize());
}

TEST(ShadingTests, EvaluatesDiffuseOnHost) {
    constexpr auto dispatcher =
        flux::BSDF::Dispatcher{.types = flux::bsdfTypeBit(flux::BSDFType::Diffuse)};
    auto const texCtx = flux::TextureEvalContext{};
    auto const bsdf = flux::BSDF::Impl{
        .type = flux::BSDFType::Diffuse,
        .storage = {
            .diffuse = {.R = constantTexture(flux::Spectrum{0.25F, 0.5F, 1.0F})},
        },
    };
    auto const result = dispatcher.execute<TestImageTextureEvaluator>(
        bsdf, texCtx, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, true, 0.5F, {0.5F, 0.5F}
    );
    auto const angledResult = dispatcher.execute<TestImageTextureEvaluator>(
        bsdf, texCtx, {0.0F, 0.0F, 1.0F}, {0.6F, 0.0F, 0.8F}, true, 0.5F, {0.5F, 0.5F}
    );
    auto const &evaluation = result.evaluation;
    auto const &angledEvaluation = angledResult.evaluation;
    auto const &sample = result.sample;

    EXPECT_NEAR(evaluation.value.x(), 0.25F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluation.value.y(), 0.5F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluation.value.z(), 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluation.pdf, 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(angledEvaluation.value.x(), 0.2F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(angledEvaluation.value.y(), 0.4F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(angledEvaluation.value.z(), 0.8F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(angledEvaluation.pdf, 0.8F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_EQ(sample.wi, (flux::Vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(sample.weight, (flux::Spectrum{0.25F, 0.5F, 1.0F}));
    EXPECT_EQ(sample.pdf, evaluation.pdf);
}

TEST(ShadingTests, EvaluatesAndSamplesDiffuseOnDevice) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::DiffuseBSDF::Impl const bsdf{.R = constantTexture(flux::Spectrum{0.25F, 0.5F, 1.0F})};
    auto const expected = bsdf.execute<TestImageTextureEvaluator>(
        {}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, true, 0.5F, {0.5F, 0.5F}
    );

    flux::DeviceBuffer<flux::BSDFEvaluation> evaluation(cudaStreamPerThread);
    flux::DeviceBuffer<flux::BSDFSample> sample(cudaStreamPerThread);
    evaluation.resize(1);
    sample.resize(1);
    flux::launchLinearKernel(
        1,
        EvaluateDiffuse{
            .bsdf = bsdf,
            .evaluation = evaluation.data(),
            .sample = sample.data(),
        },
        cudaStreamPerThread
    );

    std::array<flux::BSDFEvaluation, 1> evaluationResult{};
    std::array<flux::BSDFSample, 1> sampleResult{};
    evaluation.copyToHost(evaluationResult);
    sample.copyToHost(sampleResult);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_NEAR(evaluationResult[0].value.x(), expected.evaluation.value.x(), 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].value.y(), expected.evaluation.value.y(), 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].value.z(), expected.evaluation.value.z(), 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].pdf, expected.evaluation.pdf, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].wi.z(), expected.sample.wi.z(), 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].pdf, expected.sample.pdf, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].weight.x(), expected.sample.weight.x(), 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].weight.y(), expected.sample.weight.y(), 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].weight.z(), expected.sample.weight.z(), 1.0e-6F);
}

TEST(ShadingTests, MatchesPrincipledExecutionOnDevice) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    auto const bsdf =
        context->create<flux::BSDF>(principledProperties()).dynamicCast<flux::PrincipledBSDF>();
    ASSERT_NE(bsdf, nullptr);
    auto const expected = bsdf->getImpl().execute<TestImageTextureEvaluator>(
        {}, flux::Vec3f{0.3F, -0.2F, 1.0F}.normalize(),
        flux::Vec3f{0.4F, 0.1F, 0.911043F}.normalize(), true, 0.45F, {0.3F, 0.7F}
    );

    flux::DeviceBuffer<flux::BSDFResult> result(cudaStreamPerThread);
    result.resize(1);
    flux::launchLinearKernel(
        1,
        EvaluatePrincipled{
            .bsdf = bsdf->getImpl(),
            .result = result.data(),
        },
        cudaStreamPerThread
    );
    std::array<flux::BSDFResult, 1> actual{};
    result.copyToHost(actual);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    auto const pdfTolerance = [](float expected) {
        return 1.0e-6F * std::max(1.0F, std::abs(expected));
    };

    EXPECT_NEAR(actual[0].evaluation.value.x(), expected.evaluation.value.x(), 1.0e-5F);
    EXPECT_NEAR(actual[0].evaluation.value.y(), expected.evaluation.value.y(), 1.0e-5F);
    EXPECT_NEAR(actual[0].evaluation.value.z(), expected.evaluation.value.z(), 1.0e-5F);
    EXPECT_NEAR(
        actual[0].evaluation.pdf, expected.evaluation.pdf, pdfTolerance(expected.evaluation.pdf)
    );
    EXPECT_NEAR(actual[0].sample.weight.x(), expected.sample.weight.x(), 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.weight.y(), expected.sample.weight.y(), 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.weight.z(), expected.sample.weight.z(), 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.pdf, expected.sample.pdf, pdfTolerance(expected.sample.pdf));
    EXPECT_NEAR(actual[0].sample.eta, expected.sample.eta, 1.0e-5F);
    EXPECT_EQ(actual[0].sample.lobe, expected.sample.lobe);
}

TEST(ShadingTests, PreservesDiffuseReflectance) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("R", flux::Spectrum{0.25F, 0.5F, 1.0F});
    auto bsdf = context->create<flux::DiffuseBSDF>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_EQ(bsdf->getType(), flux::BSDFType::Diffuse);
    auto const R = bsdf->getR().dynamicCast<flux::ConstantTexture const>();
    ASSERT_NE(R, nullptr);
    EXPECT_EQ(R->getValue(), (flux::Spectrum{0.25F, 0.5F, 1.0F}));
}

TEST(ShadingTests, ResolvesDiffuseTextures) {
    auto context = flux::Context::create();

    kira::Properties defaults;
    auto defaultBSDF = context->create<flux::DiffuseBSDF>(defaults);
    auto defaultR = defaultBSDF->getR().dynamicCast<flux::ConstantTexture const>();
    ASSERT_NE(defaultR, nullptr);
    EXPECT_EQ(defaultR->getValue(), flux::Spectrum{0.5F});

    kira::Properties texture;
    texture.set("value", flux::Spectrum{0.25F, 0.5F, 1.0F});
    kira::Properties inlineProps;
    inlineProps.set("R", texture);
    auto inlineBSDF = context->create<flux::DiffuseBSDF>(inlineProps);
    auto inlineR = inlineBSDF->getR().dynamicCast<flux::ConstantTexture const>();
    ASSERT_NE(inlineR, nullptr);
    EXPECT_EQ(inlineR->getValue(), (flux::Spectrum{0.25F, 0.5F, 1.0F}));
    EXPECT_TRUE(inlineProps.is_all_used());

    kira::Properties invalidType;
    invalidType.set("R", true);
    EXPECT_THROW((void)context->create<flux::DiffuseBSDF>(invalidType), kira::Anyhow);
}

TEST(ShadingTests, CreatesPrincipledWithConstantParameters) {
    auto context = flux::Context::create();
    auto props = principledProperties();

    auto bsdf = context->create<flux::BSDF>(props).dynamicCast<flux::PrincipledBSDF>();

    ASSERT_NE(bsdf, nullptr);
    EXPECT_TRUE(props.is_all_used());
    EXPECT_EQ(bsdf->getType(), flux::BSDFType::Principled);
    auto const impl = bsdf->getImpl();
    EXPECT_EQ(
        impl.baseColor.eval3f<TestImageTextureEvaluator>({}), (flux::Spectrum{0.7F, 0.2F, 0.1F})
    );
    EXPECT_EQ(impl.roughness.eval1f<TestImageTextureEvaluator>({}), 0.35F);
    EXPECT_EQ(impl.metallic.eval1f<TestImageTextureEvaluator>({}), 0.2F);
    EXPECT_EQ(impl.specTrans.eval1f<TestImageTextureEvaluator>({}), 0.3F);
    EXPECT_EQ(impl.specTint.eval1f<TestImageTextureEvaluator>({}), 0.4F);
    EXPECT_EQ(impl.sheen.eval1f<TestImageTextureEvaluator>({}), 0.25F);
    EXPECT_EQ(impl.sheenTint.eval1f<TestImageTextureEvaluator>({}), 0.6F);
    EXPECT_EQ(impl.flatness.eval1f<TestImageTextureEvaluator>({}), 0.1F);
    EXPECT_EQ(impl.clearcoat.eval1f<TestImageTextureEvaluator>({}), 0.5F);
    EXPECT_EQ(impl.clearcoatRoughness.eval1f<TestImageTextureEvaluator>({}), 0.15F);
    EXPECT_EQ(impl.eta, 1.45F);
}

TEST(ShadingTests, PrincipledSetterRejectsNullAndForeignTextures) {
    auto context = flux::Context::create();
    auto other = flux::Context::create();
    auto bsdf = context->create<flux::PrincipledBSDF>(kira::Properties{});
    auto const texture = bsdf->getBaseColor();
    kira::Properties textureProperties;
    textureProperties.set("value", 0.5F);
    auto foreign = other->create<flux::ConstantTexture>(textureProperties);

    EXPECT_THROW(bsdf->setBaseColor(nullptr), std::invalid_argument);
    EXPECT_THROW(bsdf->setBaseColor(foreign), std::invalid_argument);

    EXPECT_EQ(bsdf->getBaseColor(), texture);
}

TEST(ShadingTests, ResolvesPrincipledEta) {
    auto context = flux::Context::create();
    kira::Properties conflicting;
    conflicting.set("eta", 1.5F);
    conflicting.set("specular", 0.5F);
    EXPECT_THROW((void)context->create<flux::PrincipledBSDF>(conflicting), kira::Anyhow);

    kira::Properties transmission;
    transmission.set("base_color", flux::Spectrum{1.0F});
    transmission.set("spec_trans", 1.0F);
    transmission.set("eta", 1.0F);
    auto const bsdf = context->create<flux::PrincipledBSDF>(transmission);
    EXPECT_EQ(bsdf->getImpl().eta, 1.0F);

    auto const sample = bsdf->getImpl()
                            .execute<TestImageTextureEvaluator>(
                                {}, {0.0F, 0.0F, 1.0F}, {}, false, 0.5F, {0.25F, 0.75F}
                            )
                            .sample;
    EXPECT_EQ(sample.weight, flux::Spectrum{1.0F});
    EXPECT_EQ(sample.wi, (flux::Vec3f{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(sample.pdf, 1.0F);
    EXPECT_EQ(sample.eta, 1.0F);
    EXPECT_EQ(sample.lobe, flux::BSDFLobe::DeltaTransmission);
}

TEST(ShadingTests, CreatesTheSelectedBsdfThroughTheBaseType) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("type", "diffuse");
    properties.set("R", flux::Spectrum{0.25F, 0.5F, 1.0F});

    auto bsdf = context->create<flux::BSDF>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_NE(bsdf.dynamicCast<flux::DiffuseBSDF>(), nullptr);

    auto const numObjects = context->getNumContextObjects();
    kira::Properties invalid;
    invalid.set("type", "unknown");
    EXPECT_THROW((void)context->create<flux::BSDF>(invalid), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), numObjects);
}

TEST(ShadingTests, EvaluatesOneSidedConstantEmission) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("radiance", flux::Spectrum{1.0F, 2.0F, 3.0F});
    auto edf = context->create<flux::ConstantEDF>(props);
    auto const impl = edf->getImpl();

    EXPECT_EQ(
        impl.evaluate({
            .geometricNormal = {0.0F, 0.0F, 1.0F},
            .wo = {0.0F, 0.0F, 1.0F},
        }),
        (flux::Spectrum{1.0F, 2.0F, 3.0F})
    );
    EXPECT_EQ(
        impl.evaluate({
            .geometricNormal = {0.0F, 0.0F, 1.0F},
            .wo = {0.0F, 0.0F, -1.0F},
        }),
        flux::Spectrum{}
    );
}

TEST(ShadingTests, BoundsNegativeEmissionToZero) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("radiance", flux::Spectrum{-1.0F, 0.5F, 0.0F});

    auto edf = context->create<flux::ConstantEDF>(props);
    EXPECT_EQ(edf->getRadiance(), (flux::Spectrum{0.0F, 0.5F, 0.0F}));

    edf->setRadiance({1.0F, -2.0F, 3.0F});
    EXPECT_EQ(edf->getRadiance(), (flux::Spectrum{1.0F, 0.0F, 3.0F}));
}
