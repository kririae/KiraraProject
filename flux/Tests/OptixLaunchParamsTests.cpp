#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "flux/Optix/OptixLaunchParams.h"

TEST(OptixLaunchParamsTests, MapsConsecutiveSamplesToTheSamePixel) {
    flux::OptixLaunchParams params{};
    params.accumulatedSamples = 10;
    params.sampleOffset = 100;
    params.batchSize = 4;
    params.film.width = 3;
    params.film.height = 2;

    auto const first = params.getLaunchSample(0);
    EXPECT_EQ(first.pixel, (flux::Vec2u{0, 0}));
    EXPECT_EQ(first.sampleIndex, 110);

    auto const lastSampleOfFirstPixel = params.getLaunchSample(3);
    EXPECT_EQ(lastSampleOfFirstPixel.pixel, (flux::Vec2u{0, 0}));
    EXPECT_EQ(lastSampleOfFirstPixel.sampleIndex, 113);

    auto const firstSampleOfSecondPixel = params.getLaunchSample(4);
    EXPECT_EQ(firstSampleOfSecondPixel.pixel, (flux::Vec2u{1, 0}));
    EXPECT_EQ(firstSampleOfSecondPixel.sampleIndex, 110);

    auto const last = params.getLaunchSample(23);
    EXPECT_EQ(last.pixel, (flux::Vec2u{2, 1}));
    EXPECT_EQ(last.sampleIndex, 113);
}

TEST(OptixLaunchParamsTests, WeightsTheNewBatchAgainstExistingSamples) {
    flux::OptixLaunchParams params{};
    params.accumulatedSamples = 12;
    params.batchSize = 4;

    EXPECT_FLOAT_EQ(params.getSampleWeight(), 1.0F / 16.0F);
}

TEST(OptixLaunchParamsTests, ClaimsChunksPastTheLaunchInOrder) {
    flux::OptixLaunchParams params{};
    params.pathCount = 400;
    params.chunkCount = 100;

    EXPECT_EQ(params.getChunkBegin(0, 10, 4), 40);
    EXPECT_EQ(params.getChunkBegin(1, 10, 4), 44);
    EXPECT_EQ(params.getChunkBegin(89, 10, 4), 396);
}

TEST(OptixLaunchParamsTests, ClaimsNothingPastTheLastChunk) {
    flux::OptixLaunchParams params{};
    params.pathCount = 398;
    params.chunkCount = 100;

    EXPECT_EQ(params.getChunkBegin(90, 10, 4), 398);
    EXPECT_EQ(params.getChunkBegin(1000, 10, 4), 398);
    EXPECT_EQ(params.getChunkBegin(std::numeric_limits<std::uint32_t>::max(), 10, 4), 398);
}

TEST(OptixLaunchParamsTests, ClaimsNothingWhereThePathIndexWouldWrap) {
    constexpr std::uint32_t chunkSize = 64;
    constexpr std::uint32_t launchSize = 1000;
    flux::OptixLaunchParams params{};
    params.pathCount = std::numeric_limits<std::uint32_t>::max();
    params.chunkCount = std::uint32_t{1} << 26U;

    // The last chunk starts below the path count, and the chunk after it would start at 2^32.
    auto const last = params.chunkCount - launchSize - 1;
    EXPECT_EQ(
        params.getChunkBegin(last, launchSize, chunkSize), (params.chunkCount - 1) * chunkSize
    );
    EXPECT_EQ(params.getChunkBegin(last + 1, launchSize, chunkSize), params.pathCount);
    EXPECT_EQ(params.getChunkBegin(last + 500, launchSize, chunkSize), params.pathCount);
}
