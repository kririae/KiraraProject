#include <cuda_runtime_api.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
        (void)tx.create<flux::IndependentSampler>(kira::Properties{});
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
    result->first = sampler.get2D(flux::SampleUse::Pixel, 0);
    result->next = sampler.get1D(flux::SampleUse::Terminate, 0);

    sampler.startPixelSample(pixel, 7U, resolution);
    result->repeated = sampler.get2D(flux::SampleUse::Pixel, 0);
    result->type = sampler.type;

    sampler.startPixelSample(pixel, 8U, resolution);
    result->differentSample = sampler.get2D(flux::SampleUse::Pixel, 0);

    sampler.startPixelSample({4U, 2U}, 7U, resolution);
    result->differentPixel = sampler.get2D(flux::SampleUse::Pixel, 0);
}

template <std::size_t Dims> using Point = std::array<float, Dims>;

flux::Sampler::Impl makeSampler(std::string_view type) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("type", std::string{type});
    return context->create<flux::Sampler>(properties)->getImpl({16U, 9U});
}

/// Draws samples 0 to count - 1 of one dimension set of a pixel.
template <std::size_t Dims>
std::vector<Point<Dims>>
drawSobol(flux::Vec2u pixel, flux::SampleUse use, std::uint32_t depth, std::uint32_t count) {
    auto sampler = makeSampler("sobol");
    std::vector<Point<Dims>> points;
    for (std::uint32_t i = 0; i < count; ++i) {
        sampler.startPixelSample(pixel, i, {16U, 9U});
        if constexpr (Dims == 1) {
            points.push_back({sampler.get1D(use, depth)});
        } else if constexpr (Dims == 2) {
            auto const u = sampler.get2D(use, depth);
            points.push_back({u.x(), u.y()});
        } else {
            auto const u = sampler.get3D(use, depth);
            points.push_back({u.x(), u.y(), u.z()});
        }
    }
    return points;
}

/// \brief Tests whether \p points form a (t, m, s)-net in base 2.
///
/// Every elementary interval of volume 2^(t - m) must hold exactly 2^t points. Intervals of the
/// largest shapes suffice, since each coarser interval is a union of them.
template <std::size_t Dims> bool isNet(std::vector<Point<Dims>> const &points, int t) {
    auto const m = static_cast<int>(std::log2(points.size()));
    std::array<int, Dims> shape{};
    auto check = [&](auto &&self, std::size_t dim, int left) -> bool {
        if (dim + 1 == Dims) {
            shape[dim] = left;
            std::vector<int> counts(std::size_t{1} << (m - t), 0);
            for (auto const &point : points) {
                std::size_t cell = 0;
                for (std::size_t d = 0; d < Dims; ++d) {
                    EXPECT_GE(point[d], 0.0F);
                    EXPECT_LT(point[d], 1.0F);
                    auto const slot = static_cast<std::size_t>(std::ldexp(point[d], shape[d]));
                    cell = (cell << shape[d]) | slot;
                }
                ++counts[cell];
            }
            for (auto const count : counts)
                if (count != (1 << t))
                    return false;
            return true;
        }
        for (int bits = 0; bits <= left; ++bits) {
            shape[dim] = bits;
            if (!self(self, dim + 1, left - bits))
                return false;
        }
        return true;
    };
    return check(check, 0, m - t);
}

template <std::size_t Dims>
float correlation(std::vector<Point<Dims>> const &a, std::vector<Point<Dims>> const &b) {
    double sa = 0.0, sb = 0.0, sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sa += a[i][0];
        sb += b[i][0];
        sab += a[i][0] * b[i][0];
        saa += a[i][0] * a[i][0];
        sbb += b[i][0] * b[i][0];
    }
    auto const n = static_cast<double>(a.size());
    auto const covariance = sab / n - sa / n * (sb / n);
    return static_cast<float>(
        covariance / std::sqrt((saa / n - sa / n * (sa / n)) * (sbb / n - sb / n * (sb / n)))
    );
}

struct SobolDraw {
    float u1;
    flux::Vec2f u2;
    flux::Vec3f u3;
};

__global__ void drawSobolOnDevice(flux::Sampler::Impl sampler, SobolDraw *draws, int count) {
    for (int i = 0; i < count; ++i) {
        sampler.startPixelSample({5U, 3U}, static_cast<std::uint64_t>(i), {16U, 9U});
        draws[i] = {
            .u1 = sampler.get1D(flux::SampleUse::Terminate, 4),
            .u2 = sampler.get2D(flux::SampleUse::Pixel, 0),
            .u3 = sampler.get3D(flux::SampleUse::Bsdf, 2),
        };
    }
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

TEST(SamplerTests, ActivatesOnlyTheSamplerTheHostSets) {
    auto context = flux::Context::create();

    EXPECT_FALSE(context->getActiveSampler());
    EXPECT_THROW(
        (void)context->create<ThrowingSamplerOwner>(kira::Properties{}), std::runtime_error
    );
    EXPECT_EQ(context->getNumContextObjects(), 0);

    auto first = context->create<flux::IndependentSampler>(kira::Properties{});
    EXPECT_FALSE(context->getActiveSampler());

    context->setActiveSampler(first);
    EXPECT_EQ(context->getActiveSampler().get(), first.get());
}

TEST(SamplerTests, CreatesTheSelectedSamplerThroughTheBaseType) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("type", "independent");

    auto sampler = context->create<flux::Sampler>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_NE(sampler.dynamicCast<flux::IndependentSampler>(), nullptr);

    kira::Properties invalid;
    invalid.set("type", "unknown");
    EXPECT_THROW((void)context->create<flux::Sampler>(invalid), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), 1);
}

TEST(SamplerTests, DispatchesDeterministicPixelSequencesOnHost) {
    auto context = flux::Context::create();
    auto sampler = context->create<flux::Sampler>(kira::Properties{})->getImpl({16U, 9U});

    sampler.startPixelSample({3U, 2U}, 7U, {16U, 9U});
    auto const first = sampler.get2D(flux::SampleUse::Pixel, 0);
    sampler.startPixelSample({3U, 2U}, 7U, {16U, 9U});

    EXPECT_EQ(sampler.type, flux::SamplerType::Independent);
    EXPECT_EQ(sampler.get2D(flux::SampleUse::Pixel, 0), first);
}

TEST(SamplerTests, DispatchesDeterministicPixelSequences) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    auto const sampler = context->create<flux::Sampler>(kira::Properties{});
    auto const resolution = flux::Vec2u{16U, 9U};
    flux::DeviceBuffer<SamplerResult> deviceResult(cudaStreamPerThread);
    deviceResult.resize(1);
    sampleIndependentSampler<<<1, 1, 0, cudaStreamPerThread>>>(
        sampler->getImpl(resolution), deviceResult.data()
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

TEST(SamplerTests, CreatesTheSobolSamplerThroughTheBaseType) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("type", "sobol");

    auto sampler = context->create<flux::Sampler>(properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_NE(sampler.dynamicCast<flux::SobolSampler>(), nullptr);
    EXPECT_EQ(sampler->getImpl({16U, 9U}).type, flux::SamplerType::Sobol);
}

TEST(SamplerTests, SobolPrefixesAreNets) {
    using flux::SampleUse;
    for (auto const pixel : {flux::Vec2u{0U, 0U}, flux::Vec2u{5U, 3U}, flux::Vec2u{15U, 8U}}) {
        for (auto const [use, depth] :
             {std::pair{SampleUse::Pixel, 0U}, std::pair{SampleUse::Bsdf, 0U},
              std::pair{SampleUse::Light, 7U}, std::pair{SampleUse::Terminate, 31U}}) {
            for (std::uint32_t m = 0; m <= 10; ++m) {
                auto const count = 1U << m;
                EXPECT_TRUE(isNet(drawSobol<1>(pixel, use, depth, count), 0)) << "1D m=" << m;
                EXPECT_TRUE(isNet(drawSobol<2>(pixel, use, depth, count), 0)) << "2D m=" << m;
                if (m >= 1)
                    EXPECT_TRUE(isNet(drawSobol<3>(pixel, use, depth, count), 1)) << "3D m=" << m;
            }
        }
    }
}

TEST(SamplerTests, SobolFullPeriodIsANet) {
    // Short prefixes leave the high index bits constant, and their direction numbers then only
    // offset a block the scramble already randomizes. The full period exercises all of them.
    constexpr auto count = flux::SobolSampler::Impl::maxSamplesPerPixel;
    auto const pixel = flux::Vec2u{5U, 3U};
    EXPECT_TRUE(isNet(drawSobol<1>(pixel, flux::SampleUse::Terminate, 3, count), 0));
    EXPECT_TRUE(isNet(drawSobol<2>(pixel, flux::SampleUse::Pixel, 0, count), 0));
    EXPECT_TRUE(isNet(drawSobol<3>(pixel, flux::SampleUse::Bsdf, 2, count), 1));
}

TEST(SamplerTests, SobolDecorrelatesDimensionSets) {
    using flux::SampleUse;
    constexpr auto count = 4096U;
    auto const pixel = flux::Vec2u{5U, 3U};
    auto const light = drawSobol<3>(pixel, SampleUse::Light, 0, count);
    auto const bsdf = drawSobol<3>(pixel, SampleUse::Bsdf, 0, count);
    auto const nextBsdf = drawSobol<3>(pixel, SampleUse::Bsdf, 1, count);
    auto const otherPixel = drawSobol<3>({6U, 3U}, SampleUse::Bsdf, 0, count);
    auto const filter = drawSobol<2>(pixel, SampleUse::Pixel, 0, count);
    auto const lens = drawSobol<2>(pixel, SampleUse::Lens, 0, count);

    EXPECT_LT(std::abs(correlation(light, bsdf)), 0.05F);
    EXPECT_LT(std::abs(correlation(bsdf, nextBsdf)), 0.05F);
    EXPECT_LT(std::abs(correlation(bsdf, otherPixel)), 0.05F);
    EXPECT_LT(std::abs(correlation(filter, lens)), 0.05F);
}

TEST(SamplerTests, SobolMatchesOnHostAndDevice) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    constexpr int count = 256;
    auto sampler = makeSampler("sobol");
    flux::DeviceBuffer<SobolDraw> deviceDraws(cudaStreamPerThread);
    deviceDraws.resize(count);
    drawSobolOnDevice<<<1, 1, 0, cudaStreamPerThread>>>(sampler, deviceDraws.data(), count);
    flux::cudaCheck(cudaGetLastError());
    std::vector<SobolDraw> draws(count);
    deviceDraws.copyToHost({draws.data(), draws.size()});
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    for (int i = 0; i < count; ++i) {
        sampler.startPixelSample({5U, 3U}, static_cast<std::uint64_t>(i), {16U, 9U});
        EXPECT_EQ(draws[i].u1, sampler.get1D(flux::SampleUse::Terminate, 4)) << i;
        EXPECT_EQ(draws[i].u2, sampler.get2D(flux::SampleUse::Pixel, 0)) << i;
        EXPECT_EQ(draws[i].u3, sampler.get3D(flux::SampleUse::Bsdf, 2)) << i;
    }

    deviceDraws.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}
