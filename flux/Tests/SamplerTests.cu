#include <cuda_runtime_api.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Sampling/SamplerImpl.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace {
class ThrowingSamplerOwner final : public flux::RenderObject {
    friend class flux::TXContext;

    ThrowingSamplerOwner(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        (void)tx.create<flux::IndependentSampler>();
        throw std::runtime_error("intentional sampler transaction failure");
    }
};

struct SamplerResult {
    flux::Vec2f first;
    flux::Vec2f repeated;
    flux::Vec2f differentSample;
    flux::Vec2f differentPixel;
    float next;
    flux::SamplerType type;
};

__global__ void sampleIndependentSampler(flux::Sampler::Impl sampler, SamplerResult *result) {
    auto const pixel = flux::Vec2u{3U, 2U};
    auto const resolution = flux::Vec2u{16U, 9U};

    sampler.startPixelSample(pixel, 7U, resolution);
    result->first = sampler.get2D();
    result->next = sampler.get1D();

    sampler.startPixelSample(pixel, 7U, resolution);
    result->repeated = sampler.get2D();
    result->type = sampler.type;

    sampler.startPixelSample(pixel, 8U, resolution);
    result->differentSample = sampler.get2D();

    sampler.startPixelSample({4U, 2U}, 7U, resolution);
    result->differentPixel = sampler.get2D();
}
} // namespace

TEST(SamplerTests, KeepsFirstSuccessfulSamplerActive) {
    auto context = flux::Context::create();

    EXPECT_THROW((void)context->getActiveSampler(), kira::Anyhow);
    EXPECT_THROW((void)context->create<ThrowingSamplerOwner>(), std::runtime_error);
    EXPECT_THROW((void)context->getActiveSampler(), kira::Anyhow);

    auto first = context->create<flux::IndependentSampler>();
    EXPECT_EQ(context->getActiveSampler().get(), first.get());

    (void)context->create<flux::IndependentSampler>();
    EXPECT_EQ(context->getActiveSampler().get(), first.get());
}

TEST(SamplerTests, CreatesTheSelectedSamplerThroughTheBaseType) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("type", "independent");

    auto sampler = context->create<flux::Sampler>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_NE(sampler.dynamicCast<flux::IndependentSampler>(), nullptr);
    EXPECT_EQ(context->getActiveSampler(), sampler);

    kira::Properties invalid;
    invalid.set("type", "unknown");
    EXPECT_THROW((void)context->create<flux::Sampler>(invalid), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), 1);
}

TEST(SamplerTests, DispatchesDeterministicPixelSequencesOnHost) {
    auto context = flux::Context::create();
    (void)context->create<flux::IndependentSampler>();
    auto sampler = context->getActiveSampler()->getImpl({16U, 9U});

    sampler.startPixelSample({3U, 2U}, 7U, {16U, 9U});
    auto const first = sampler.get2D();
    sampler.startPixelSample({3U, 2U}, 7U, {16U, 9U});

    EXPECT_EQ(sampler.type, flux::SamplerType::Independent);
    EXPECT_EQ(sampler.get2D(), first);
}

TEST(SamplerTests, DispatchesDeterministicPixelSequences) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::IndependentSampler>();
    auto const resolution = flux::Vec2u{16U, 9U};
    flux::DeviceBuffer<SamplerResult> deviceResult(cudaStreamPerThread);
    deviceResult.resize(1);
    sampleIndependentSampler<<<1, 1, 0, cudaStreamPerThread>>>(
        context->getActiveSampler()->getImpl(resolution), deviceResult.data()
    );
    flux::cudaCheck(cudaGetLastError());

    SamplerResult result{};
    deviceResult.copyToHost({&result, 1});
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_EQ(result.type, flux::SamplerType::Independent);
    EXPECT_FLOAT_EQ(result.first.x(), result.repeated.x());
    EXPECT_FLOAT_EQ(result.first.y(), result.repeated.y());
    EXPECT_GE(result.first.x(), 0.0F);
    EXPECT_LT(result.first.x(), 1.0F);
    EXPECT_GE(result.first.y(), 0.0F);
    EXPECT_LT(result.first.y(), 1.0F);
    EXPECT_GE(result.next, 0.0F);
    EXPECT_LT(result.next, 1.0F);
    EXPECT_NE(result.first.x(), result.differentSample.x());
    EXPECT_NE(result.first.x(), result.differentPixel.x());

    deviceResult.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}

TEST(SamplerTests, SelectsBackendLightsUniformly) {
    auto const points = std::array{
        flux::PointLight::Impl{.position = {1.0F, 0.0F, 0.0F}, .intensity = {1.0F, 1.0F, 1.0F}},
        flux::PointLight::Impl{.position = {0.0F, 0.0F, 2.0F}, .intensity = {4.0F, 8.0F, 12.0F}},
    };
    auto const records = std::array{
        flux::LightRecord{.type = flux::LightType::Point, .typedIndex = 1},
        flux::LightRecord{.type = flux::LightType::Point, .typedIndex = 0},
    };
    auto const sampler = flux::LightSampler{
        .lights = {
            .records = records.data(),
            .pointLights = points.data(),
            .numLights = static_cast<std::uint32_t>(records.size()),
        },
    };
    auto const ctx = flux::LightSamplingContext{.position = {0.0F, 0.0F, 0.0F}};

    auto const first = sampler.sample(ctx, 0.0F);
    auto const last = sampler.sample(ctx, std::nextafter(1.0F, 0.0F));
    EXPECT_EQ(first.lightIndex, 0);
    EXPECT_EQ(last.lightIndex, 1);
    EXPECT_FLOAT_EQ(first.pmf, 0.5F);
    EXPECT_FLOAT_EQ(sampler.pmf(ctx, first.lightIndex), first.pmf);
    EXPECT_FLOAT_EQ(sampler.pmf(ctx, records.size()), 0.0F);

    auto const lightSample = sampler.sampleDirect(first.lightIndex, ctx, {});
    EXPECT_EQ(lightSample.wi, (flux::Vec3f{0.0F, 0.0F, 1.0F}));
    EXPECT_FLOAT_EQ(lightSample.distance, 2.0F);
    EXPECT_EQ(lightSample.radiance, (flux::Spectrum{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(lightSample.delta);

    auto const empty = flux::LightSampler{}.sample(ctx, 0.0F);
    EXPECT_EQ(empty.lightIndex, flux::SampledLight::invalidIndex);
    EXPECT_FLOAT_EQ(empty.pmf, 0.0F);
}
