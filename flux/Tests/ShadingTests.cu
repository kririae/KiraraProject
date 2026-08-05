#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <numbers>

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
struct EvaluateDiffuse {
    flux::DiffuseBSDF::Impl bsdf;
    flux::BSDFEvaluation *evaluation;
    flux::BSDFSample *sample;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        auto const isect = flux::SurfaceInteraction{
            .shadingNormal = {1.0F, 0.0F, 0.0F},
        };
        auto const result =
            bsdf.execute(isect, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, true, 0.5F, {0.5F, 0.5F});
        *evaluation = result.evaluation;
        *sample = result.sample;
    }
};

struct EvaluatePrincipled {
    flux::PrincipledBSDF::Impl bsdf;
    flux::BSDFResult *result;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        *result = bsdf.execute(
            {}, flux::Vec3f{0.3F, -0.2F, 1.0F}.normalize(),
            flux::Vec3f{0.4F, 0.1F, 0.911043F}.normalize(), true, 0.45F, {0.3F, 0.7F}
        );
    }
};

[[nodiscard]] flux::PrincipledBSDF::Impl referencePrincipled() {
    return {
        .baseColor = {0.7F, 0.2F, 0.1F},
        .roughness = 0.35F,
        .metallic = 0.2F,
        .specTrans = 0.3F,
        .specTint = 0.4F,
        .sheen = 0.25F,
        .sheenTint = 0.6F,
        .flatness = 0.2F,
        .clearcoat = 0.5F,
        .clearcoatRoughness = 0.3F,
        .eta = 1.45F,
    };
}
} // namespace

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

TEST(ShadingTests, EvaluatesDiffuseOnHost) {
    constexpr auto dispatcher =
        flux::BSDF::Dispatcher{.types = flux::bsdfTypeBit(flux::BSDFType::Diffuse)};
    auto const isect = flux::SurfaceInteraction{
        .shadingNormal = {1.0F, 0.0F, 0.0F},
    };
    auto const bsdf = flux::BSDF::Impl{
        .type = flux::BSDFType::Diffuse,
        .storage = {.diffuse = {.reflectance = {0.25F, 0.5F, 1.0F}}},
    };
    auto const result = dispatcher.execute(
        bsdf, isect, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, true, 0.5F, {0.5F, 0.5F}
    );
    auto const angledResult = dispatcher.execute(
        bsdf, isect, {0.0F, 0.0F, 1.0F}, {0.6F, 0.0F, 0.8F}, true, 0.5F, {0.5F, 0.5F}
    );
    auto const sampleOnly =
        dispatcher.execute(bsdf, isect, {0.0F, 0.0F, 1.0F}, {}, false, 0.5F, {0.5F, 0.5F});
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
    EXPECT_EQ(sampleOnly.evaluation.value, flux::Spectrum{});
    EXPECT_EQ(sampleOnly.evaluation.pdf, 0.0F);
}

TEST(ShadingTests, EvaluatesAndSamplesDiffuseOnDevice) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::DeviceBuffer<flux::BSDFEvaluation> evaluation(cudaStreamPerThread);
    flux::DeviceBuffer<flux::BSDFSample> sample(cudaStreamPerThread);
    evaluation.resize(1);
    sample.resize(1);
    flux::launchLinearKernel(
        1,
        EvaluateDiffuse{
            .bsdf = {.reflectance = {0.25F, 0.5F, 1.0F}},
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

    EXPECT_NEAR(evaluationResult[0].value.x(), 0.25F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].value.y(), 0.5F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].value.z(), 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].pdf, 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].wi.z(), 1.0F, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].pdf, 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].weight.x(), 0.25F, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].weight.y(), 0.5F, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].weight.z(), 1.0F, 1.0e-6F);
}

TEST(ShadingTests, MatchesPrincipledExecutionOnDevice) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto const bsdf = referencePrincipled();
    auto const expected = bsdf.execute(
        {}, flux::Vec3f{0.3F, -0.2F, 1.0F}.normalize(),
        flux::Vec3f{0.4F, 0.1F, 0.911043F}.normalize(), true, 0.45F, {0.3F, 0.7F}
    );
    auto const sampleOnly = bsdf.execute(
        {}, flux::Vec3f{0.3F, -0.2F, 1.0F}.normalize(), {}, false, 0.45F, {0.3F, 0.7F}
    );

    EXPECT_EQ(sampleOnly.evaluation.value, flux::Spectrum{});
    EXPECT_EQ(sampleOnly.evaluation.pdf, 0.0F);
    EXPECT_EQ(sampleOnly.sample.weight, expected.sample.weight);
    EXPECT_EQ(sampleOnly.sample.wi, expected.sample.wi);
    EXPECT_EQ(sampleOnly.sample.pdf, expected.sample.pdf);

    flux::DeviceBuffer<flux::BSDFResult> result(cudaStreamPerThread);
    result.resize(1);
    flux::launchLinearKernel(
        1,
        EvaluatePrincipled{
            .bsdf = bsdf,
            .result = result.data(),
        },
        cudaStreamPerThread
    );
    std::array<flux::BSDFResult, 1> actual{};
    result.copyToHost(actual);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_NEAR(actual[0].evaluation.value.x(), expected.evaluation.value.x(), 1.0e-5F);
    EXPECT_NEAR(actual[0].evaluation.value.y(), expected.evaluation.value.y(), 1.0e-5F);
    EXPECT_NEAR(actual[0].evaluation.value.z(), expected.evaluation.value.z(), 1.0e-5F);
    EXPECT_NEAR(actual[0].evaluation.pdf, expected.evaluation.pdf, 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.weight.x(), expected.sample.weight.x(), 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.weight.y(), expected.sample.weight.y(), 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.weight.z(), expected.sample.weight.z(), 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.pdf, expected.sample.pdf, 1.0e-5F);
    EXPECT_NEAR(actual[0].sample.eta, expected.sample.eta, 1.0e-5F);
    EXPECT_EQ(actual[0].sample.lobe, expected.sample.lobe);
}

TEST(ShadingTests, ValidatesDiffuseReflectance) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("R", flux::Spectrum{0.25F, 0.5F, 1.0F});
    auto bsdf = context->create<flux::DiffuseBSDF>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_EQ(bsdf->getType(), flux::BSDFType::Diffuse);
    EXPECT_EQ(bsdf->getReflectance(), (flux::Spectrum{0.25F, 0.5F, 1.0F}));

    kira::Properties invalid;
    invalid.set("R", flux::Spectrum{0.0F, 0.5F, 1.1F});
    EXPECT_THROW((void)context->create<flux::DiffuseBSDF>(invalid), kira::Anyhow);
}

TEST(ShadingTests, CreatesPrincipledWithConstantParameters) {
    auto context = flux::Context::create();
    kira::Properties props;
    props.set("type", "principled");
    props.set("base_color", flux::Spectrum{0.7F, 0.2F, 0.1F});
    props.set("roughness", 0.35F);
    props.set("metallic", 0.2F);
    props.set("spec_trans", 0.3F);
    props.set("spec_tint", 0.4F);
    props.set("sheen", 0.25F);
    props.set("sheen_tint", 0.6F);
    props.set("flatness", 0.2F);
    props.set("clearcoat", 0.5F);
    props.set("clearcoat_roughness", 0.3F);
    props.set("eta", 1.45F);

    auto bsdf = context->create<flux::BSDF>(props).dynamicCast<flux::PrincipledBSDF>();

    ASSERT_NE(bsdf, nullptr);
    EXPECT_TRUE(props.is_all_used());
    EXPECT_EQ(bsdf->getType(), flux::BSDFType::Principled);
    auto const impl = bsdf->getImpl();
    EXPECT_EQ(impl.baseColor, (flux::Spectrum{0.7F, 0.2F, 0.1F}));
    EXPECT_EQ(impl.roughness, 0.35F);
    EXPECT_EQ(impl.eta, 1.45F);
}

TEST(ShadingTests, ValidatesPrincipledParameters) {
    auto context = flux::Context::create();
    kira::Properties conflicting;
    conflicting.set("eta", 1.5F);
    conflicting.set("specular", 0.5F);
    EXPECT_THROW((void)context->create<flux::PrincipledBSDF>(conflicting), kira::Anyhow);

    kira::Properties invalidRoughness;
    invalidRoughness.set("roughness", -0.1F);
    EXPECT_THROW((void)context->create<flux::PrincipledBSDF>(invalidRoughness), kira::Anyhow);

    kira::Properties infiniteRoughness;
    infiniteRoughness.set("roughness", std::numeric_limits<float>::infinity());
    EXPECT_THROW((void)context->create<flux::PrincipledBSDF>(infiniteRoughness), kira::Anyhow);

    kira::Properties nanEta;
    nanEta.set("eta", std::numeric_limits<float>::quiet_NaN());
    EXPECT_THROW((void)context->create<flux::PrincipledBSDF>(nanEta), kira::Anyhow);

    kira::Properties transmission;
    transmission.set("spec_trans", 1.0F);
    transmission.set("eta", 1.0F);
    auto const bsdf = context->create<flux::PrincipledBSDF>(transmission);
    EXPECT_EQ(bsdf->getImpl().eta, 1.001F);
}

TEST(ShadingTests, CreatesTheSelectedBsdfThroughTheBaseType) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("type", "diffuse");
    properties.set("R", flux::Spectrum{0.25F, 0.5F, 1.0F});

    auto bsdf = context->create<flux::BSDF>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_NE(bsdf.dynamicCast<flux::DiffuseBSDF>(), nullptr);

    kira::Properties invalid;
    invalid.set("type", "unknown");
    EXPECT_THROW((void)context->create<flux::BSDF>(invalid), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), 1);
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

    kira::Properties invalid;
    invalid.set("radiance", flux::Spectrum{-1.0F, 0.0F, 0.0F});
    EXPECT_THROW((void)context->create<flux::ConstantEDF>(invalid), kira::Anyhow);
}
