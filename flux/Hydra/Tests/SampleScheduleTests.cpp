#include <gtest/gtest.h>

#include <climits>
#include <optional>

#include "flux/Hydra/SampleSchedule.h"

TEST(SampleScheduleTests, AddsAtMostTheInteractiveSamplesPerLaunchAndStopsAtTheTarget) {
    HdFLuxSampleSchedule const schedule{10, 4};

    EXPECT_EQ(schedule.GetNextSampleCount(0, 1U), 4U);
    EXPECT_EQ(schedule.GetNextSampleCount(8, 1U), 2U);
    EXPECT_EQ(schedule.GetNextSampleCount(10, 1U), 0U);
}

TEST(SampleScheduleTests, AddsAllTheRemainingSamplesToABatchLaunchWithinThePathIndexLimit) {
    using Mode = HdFLuxSampleSchedule::Mode;

    HdFLuxSampleSchedule const schedule{10, 4, Mode::Batch};

    EXPECT_EQ(schedule.GetNextSampleCount(0, 1U), 10U);
    EXPECT_EQ(schedule.GetNextSampleCount(8, 1U), 2U);
    EXPECT_EQ(schedule.GetNextSampleCount(10, 1U), 0U);

    HdFLuxSampleSchedule const large{INT_MAX, 1, Mode::Batch};

    EXPECT_EQ(large.GetNextSampleCount(0, std::uint64_t{1} << 20U), 4095U);
}

TEST(SampleScheduleTests, KeepsAnInteractiveLaunchWithinThePathIndexLimit) {
    HdFLuxSampleSchedule const schedule{INT_MAX, INT_MAX};

    // A launch has at most 2^32 - 1 paths.
    EXPECT_EQ(schedule.GetNextSampleCount(0, std::uint64_t{1} << 20U), 4095U);
    // A film with more pixels than that gets one sample.
    EXPECT_EQ(schedule.GetNextSampleCount(0, std::uint64_t{1} << 33U), 1U);
}

TEST(SampleScheduleTests, DenoisesAtTheStartSampleThenEveryLaunchesPerDenoiseAndAtTheTarget) {
    using Mode = HdFLuxSampleSchedule::Mode;
    using Cadence = HdFLuxSampleSchedule::DenoiseCadence;
    using enum HdFLuxSampleSchedule::Publication;

    HdFLuxSampleSchedule const noisy{16, 1};
    EXPECT_EQ(noisy.GetPublication(4, std::nullopt), Noisy);
    EXPECT_EQ(noisy.GetPublication(16, std::nullopt), Noisy);

    HdFLuxSampleSchedule const schedule{16, 1, Mode::Interactive, Cadence{2, 4}};
    EXPECT_EQ(schedule.GetPublication(1, std::nullopt), Noisy);
    EXPECT_EQ(schedule.GetPublication(2, std::nullopt), Denoised);
    EXPECT_EQ(schedule.GetPublication(3, 1U), None);
    EXPECT_EQ(schedule.GetPublication(5, 3U), None);
    EXPECT_EQ(schedule.GetPublication(6, 4U), Denoised);
    EXPECT_EQ(schedule.GetPublication(16, 1U), Denoised);
    EXPECT_EQ(schedule.GetPublication(20, 1U), Denoised);

    // A start sample above the target still denoises at the target.
    HdFLuxSampleSchedule const late{16, 1, Mode::Interactive, Cadence{32, 4}};
    EXPECT_EQ(late.GetPublication(16, std::nullopt), Denoised);

    HdFLuxSampleSchedule const everyLaunch{16, 1, Mode::Interactive, Cadence{2, 0}};
    EXPECT_EQ(everyLaunch.GetPublication(3, 1U), Denoised);
}
