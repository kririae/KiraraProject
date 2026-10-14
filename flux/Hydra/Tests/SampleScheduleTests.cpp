#include <gtest/gtest.h>

#include <climits>

#include "flux/Hydra/SampleSchedule.h"

TEST(SampleScheduleTests, AddsAtMostTheInteractiveSamplesPerLaunchAndStopsAtTheTarget) {
    HdFluxSampleSchedule const schedule{10, 4};

    EXPECT_EQ(schedule.GetNextSampleCount(0, 1U), 4U);
    EXPECT_EQ(schedule.GetNextSampleCount(8, 1U), 2U);
    EXPECT_EQ(schedule.GetNextSampleCount(10, 1U), 0U);
}

TEST(SampleScheduleTests, AddsAllTheRemainingSamplesToABatchLaunchWithinThePathIndexLimit) {
    using Mode = HdFluxSampleSchedule::Mode;

    HdFluxSampleSchedule const schedule{10, 4, Mode::Batch};

    EXPECT_EQ(schedule.GetNextSampleCount(0, 1U), 10U);
    EXPECT_EQ(schedule.GetNextSampleCount(8, 1U), 2U);
    EXPECT_EQ(schedule.GetNextSampleCount(10, 1U), 0U);

    HdFluxSampleSchedule const large{INT_MAX, 1, Mode::Batch};

    EXPECT_EQ(large.GetNextSampleCount(0, std::uint64_t{1} << 20U), 4095U);
}

TEST(SampleScheduleTests, KeepsAnInteractiveLaunchWithinThePathIndexLimit) {
    HdFluxSampleSchedule const schedule{INT_MAX, INT_MAX};

    // A launch has at most 2^32 - 1 paths.
    EXPECT_EQ(schedule.GetNextSampleCount(0, std::uint64_t{1} << 20U), 4095U);
    // A film with more pixels than that gets one sample.
    EXPECT_EQ(schedule.GetNextSampleCount(0, std::uint64_t{1} << 33U), 1U);
}
