#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <numbers>
#include <utility>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/DiffuseBSDF.cuh"
#include "flux/Shading/Frame.h"
#include "kira/Anyhow.h"

namespace {
struct EvaluateDiffuse {
    flux::DiffuseBSDF::DeviceImpl bsdf;
    flux::BSDFEvaluation *evaluation;
    flux::BSDFSample *sample;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        auto const surface = flux::SurfaceInteraction{
            .shadingNormal = {0.0F, 0.0F, 1.0F},
        };
        auto const query = flux::BSDFQuery{
            .surface = surface,
            .wo = {0.0F, 0.0F, 1.0F},
        };
        *evaluation = bsdf.evaluateAndPdf(query, {0.0F, 0.0F, 1.0F});
        *sample = bsdf.sample(query, 0.5F, {0.5F, 0.5F});
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

    EXPECT_NEAR(evaluationResult[0].f.x(), 0.25F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].f.y(), 0.5F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].f.z(), 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(evaluationResult[0].pdf, 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].wi.z(), 1.0F, 1.0e-6F);
    EXPECT_NEAR(sampleResult[0].pdf, 1.0F / std::numbers::pi_v<float>, 1.0e-6F);
    auto const albedo = sampleResult[0].f * (sampleResult[0].wi.z() / sampleResult[0].pdf);
    EXPECT_NEAR(albedo.x(), 0.25F, 1.0e-6F);
    EXPECT_NEAR(albedo.y(), 0.5F, 1.0e-6F);
    EXPECT_NEAR(albedo.z(), 1.0F, 1.0e-6F);
}

TEST(ShadingTests, ValidatesDiffuseReflectance) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("reflectance", flux::Spectrum{0.25F, 0.5F, 1.0F});
    auto bsdf = context->create<flux::DiffuseBSDF>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_EQ(bsdf->getType(), flux::BSDFType::Diffuse);
    EXPECT_EQ(bsdf->getReflectance(), (flux::Spectrum{0.25F, 0.5F, 1.0F}));

    kira::Properties invalid;
    invalid.set("reflectance", flux::Spectrum{0.0F, 0.5F, 1.1F});
    EXPECT_THROW((void)context->create<flux::DiffuseBSDF>(std::move(invalid)), kira::Anyhow);
}
