#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <numbers>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/DiffuseBSDFImpl.h"
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
            .shadingNormal = {0.0F, 0.0F, 1.0F},
        };
        auto const wo = flux::Vec3f{0.0F, 0.0F, 1.0F};
        auto const query = flux::BSDFQuery{.wo = wo};
        bsdf.init(isect, wo, [&](auto const &bsdf, auto const &bsdfState) {
            *evaluation = bsdf.evaluateAndPdf(bsdfState, query, flux::Vec3f{0.0F, 0.0F, 1.0F});
            *sample = bsdf.sample(bsdfState, query, 0.5F, {0.5F, 0.5F});
        });
    }
};
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
    auto const isect = flux::SurfaceInteraction{
        .shadingNormal = {0.0F, 0.0F, 1.0F},
    };
    auto const bsdf = flux::BSDF::Impl{flux::DiffuseBSDF::Impl{
        .reflectance = {0.25F, 0.5F, 1.0F},
    }};
    auto const wo = flux::Vec3f{0.0F, 0.0F, 1.0F};
    auto const query = flux::BSDFQuery{.wo = wo};
    flux::BSDFEvaluation evaluation;
    flux::BSDFEvaluation angledEvaluation;
    flux::BSDFSample sample;
    auto const callbackResult = bsdf.init(isect, wo, [&](auto const &bsdf, auto const &bsdfState) {
        evaluation = bsdf.evaluateAndPdf(bsdfState, query, flux::Vec3f{0.0F, 0.0F, 1.0F});
        angledEvaluation = bsdf.evaluateAndPdf(bsdfState, query, flux::Vec3f{0.6F, 0.0F, 0.8F});
        sample = bsdf.sample(bsdfState, query, 0.5F, {0.5F, 0.5F});
        return 42;
    });

    EXPECT_EQ(callbackResult, 42);
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
