#include <gtest/gtest.h>

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
