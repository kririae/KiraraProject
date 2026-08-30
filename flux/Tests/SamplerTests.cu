#include <cuda_runtime_api.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Distribution2D.h"
#include "flux/Sampling/LightPowerDistribution.h"
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

TEST(SamplerTests, MatchesUpperBoundAcrossTableSizes) {
    auto const values = std::array{0.0F, 1.0F, 1.0F, 3.0F, 7.0F, 8.0F, 13.0F, 21.0F, 34.0F};
    auto const targets = std::array{-1.0F, 0.0F, 1.0F, 2.0F, 7.0F, 20.0F, 34.0F, 35.0F};

    for (std::size_t size = 0; size <= values.size(); ++size) {
        for (auto const target : targets) {
            auto const expected = static_cast<std::size_t>(
                std::upper_bound(values.begin(), values.begin() + size, target) - values.begin()
            );
            EXPECT_EQ(flux::upperBoundIndex(values.data(), size, target), expected);
        }
    }
}

TEST(SamplerTests, ProducesTriangleBarycentricsFromScalarSamples) {
    auto const samples = std::array{0.0F, 0.125F, 0.5F, std::nextafter(1.0F, 0.0F)};

    for (auto const sample : samples) {
        auto const barycentric = flux::lowDiscrepancySampleTriangle(sample);
        EXPECT_GE(barycentric.x(), 0.0F);
        EXPECT_GE(barycentric.y(), 0.0F);
        EXPECT_LE(barycentric.x() + barycentric.y(), 1.0F);
    }
}

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

TEST(SamplerTests, SelectsLightsByPower) {
    auto const powers = std::array{1.0F, 2.0F};
    auto const powerCDF = flux::buildLightPowerCDF(powers);
    auto const sampler = flux::LightPowerDistribution{
        .cdf = powerCDF.data(),
        .sum = powerCDF.back(),
        .numLights = static_cast<std::uint32_t>(powers.size()),
    };
    float firstPMF;
    float lastPMF;
    auto const first = sampler.sample(0.0F, firstPMF);
    auto const last = sampler.sample(std::nextafter(1.0F, 0.0F), lastPMF);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(last, 1);
    EXPECT_FLOAT_EQ(firstPMF, 1.0F / 3.0F);
    EXPECT_FLOAT_EQ(lastPMF, 2.0F / 3.0F);
    EXPECT_FLOAT_EQ(sampler.pmf(first), firstPMF);
    EXPECT_FLOAT_EQ(sampler.pmf(powers.size()), 0.0F);

    auto const single = flux::LightPowerDistribution{.numLights = 1};
    float singlePMF;
    EXPECT_EQ(single.sample(0.5F, singlePMF), 0);
    EXPECT_FLOAT_EQ(singlePMF, 1.0F);
    EXPECT_FLOAT_EQ(single.pmf(0), 1.0F);

    float emptyPMF;
    auto const empty = flux::LightPowerDistribution{}.sample(0.0F, emptyPMF);
    EXPECT_EQ(empty, std::numeric_limits<std::uint32_t>::max());
    EXPECT_FLOAT_EQ(emptyPMF, 0.0F);
}

TEST(SamplerTests, FallsBackToUniformSelectionWhenAllPowersAreZero) {
    auto const powers = std::array{0.0F, 0.0F, 0.0F};
    auto const powerCDF = flux::buildLightPowerCDF(powers);

    EXPECT_EQ(powerCDF, (std::vector<float>{1.0F / 3.0F, 2.0F / 3.0F, 1.0F}));
}

TEST(SamplerTests, BoundsNegativeLightPowers) {
    auto const powers = std::array{-1.0F, 0.0F, 1.0F};
    auto const powerCDF = flux::buildLightPowerCDF(powers);

    EXPECT_EQ(powerCDF, (std::vector<float>{0.0F, 0.0F, 1.0F}));
}

TEST(SamplerTests, KeepsPositiveLightsSelectableAcrossLargePowerRatios) {
    auto const powers = std::array{std::numeric_limits<float>::max(), 1.0F};
    auto const powerCDF = flux::buildLightPowerCDF(powers);
    auto const sampler = flux::LightPowerDistribution{
        .cdf = powerCDF.data(),
        .sum = powerCDF.back(),
        .numLights = static_cast<std::uint32_t>(powers.size()),
    };
    float pmf;
    auto const sample = sampler.sample(std::nextafter(1.0F, 0.0F), pmf);
    EXPECT_EQ(sample, 1);
    EXPECT_GT(pmf, 0.0F);
}

TEST(SamplerTests, BuildsAndSamplesTwoDimensionalDistributions) {
    auto weights = std::array{1.0F, 3.0F, 0.0F, 0.0F};
    auto rows = std::array<float, 2>{};

    EXPECT_FLOAT_EQ(flux::buildCDF2D({2, 2}, weights, rows), 4.0F);
    auto const dist = flux::Distribution2D{
        .condCDF = weights.data(),
        .rowCDF = rows.data(),
        .extent = {2, 2},
    };

    EXPECT_FLOAT_EQ(dist.pdf({0.25F, 0.25F}), 1.0F);
    EXPECT_FLOAT_EQ(dist.pdf({0.75F, 0.25F}), 3.0F);
    EXPECT_FLOAT_EQ(dist.pdf({0.25F, 0.75F}), 0.0F);

    float pdf;
    auto const sample = dist.sample({0.5F, 0.5F}, pdf);
    EXPECT_GE(sample.x(), 0.5F);
    EXPECT_LT(sample.x(), 1.0F);
    EXPECT_GE(sample.y(), 0.0F);
    EXPECT_LT(sample.y(), 0.5F);
    EXPECT_FLOAT_EQ(pdf, 3.0F);
}

TEST(SamplerTests, BuildsTwoDimensionalDistributionsFromLargeWeights) {
    auto weights = std::array{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    auto rows = std::array<float, 1>{};

    EXPECT_FLOAT_EQ(flux::buildCDF2D({3, 1}, weights, rows), std::numeric_limits<float>::max());
    EXPECT_NEAR(weights[0], 1.0F / 3.0F, 1.0e-6F);
    EXPECT_NEAR(weights[1], 2.0F / 3.0F, 1.0e-6F);
    EXPECT_FLOAT_EQ(weights[2], 1.0F);
    EXPECT_FLOAT_EQ(rows[0], 1.0F);
}

TEST(SamplerTests, BoundsNegativeTwoDimensionalWeights) {
    auto weights = std::array{-1.0F, 0.0F, 1.0F};
    auto rows = std::array<float, 1>{};

    EXPECT_FLOAT_EQ(flux::buildCDF2D({3, 1}, weights, rows), 1.0F);
    EXPECT_EQ(weights, (std::array{0.0F, 0.0F, 1.0F}));
    EXPECT_FLOAT_EQ(rows[0], 1.0F);
}
