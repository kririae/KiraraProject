#include <gtest/gtest.h>

#include "Scene/Animation.h"
#include "Scene/SceneGraph.h"

using namespace krd;
using enum AnimationBehaviour;

class AnimationSequenceTests : public ::testing::Test {
protected:
    AnimationSequence<float> emptySeq;
    AnimationSequence<float> singleEltSeq;
    AnimationSequence<float> multiEltSeq;

    void SetUp() override {
        singleEltSeq.push_back({0.0f, 1.0f});

        multiEltSeq.push_back({0.0f, 1.0f});
        multiEltSeq.push_back({1.0f, 2.0f});
        multiEltSeq.push_back({2.0f, 3.0f});
    }
};

TEST_F(AnimationSequenceTests, StartTime) {
    EXPECT_FLOAT_EQ(emptySeq.getStartTime(), 0.0f);
    EXPECT_FLOAT_EQ(singleEltSeq.getStartTime(), 0.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getStartTime(), 0.0f);
}

TEST_F(AnimationSequenceTests, EndTime) {
    EXPECT_FLOAT_EQ(emptySeq.getEndTime(), 0.0f);
    EXPECT_FLOAT_EQ(singleEltSeq.getEndTime(), 0.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getEndTime(), 2.0f);
}

TEST_F(AnimationSequenceTests, SingleElement) {
    float const defValue = 0.0f;

    // Default
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(-1.0f, Default, Constant, defValue), defValue);
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(1.0f, Constant, Default, defValue), defValue);

    // Constant
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(-1.0f, Constant, Constant, defValue), 1.0f);
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(0.0f, Constant, Constant, defValue), 1.0f);
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(1.0f, Constant, Constant, defValue), 1.0f);

    // Repeat
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(-1.0f, Repeat, Repeat, defValue), 1.0f);
    EXPECT_FLOAT_EQ(singleEltSeq.getAtTime(1.0f, Repeat, Repeat, defValue), 1.0f);
}

TEST_F(AnimationSequenceTests, MultiElement) {
    float const defValue = 0.0f;

    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(-1.0f, Constant, Constant, defValue), 1.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(0.5f, Linear, Linear, defValue), 1.5f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(1.0f, Linear, Linear, defValue), 2.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(3.0f, Constant, Constant, defValue), 3.0f);
}

TEST_F(AnimationSequenceTests, EmptySequence) {
    float const defValue = 5.0f;
    EXPECT_FLOAT_EQ(emptySeq.getAtTime(0.0f, Constant, Constant, defValue), defValue);
}

TEST_F(AnimationSequenceTests, RepeatBehaviour) {
    float const defValue = 0.0f;

    for (float v = -4.0f; v <= 6.0f; v += 0.3f) {
        auto expected = 1.0f + std::fmod(std::fmod(v, 2.0f) + 2.0f, 2.0f);
        EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(v, Repeat, Repeat, defValue), expected);
    }

    // No recurse
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(0.0f, Repeat, Repeat, defValue), 1.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(1.99999f, Repeat, Repeat, defValue), 2.99999f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(2.0f, Repeat, Repeat, defValue), 1.0f);
}

TEST_F(AnimationSequenceTests, BoundaryConditions) {
    float const defValue = 0.0f;

    // Exact start time
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(0.0f, Constant, Constant, defValue), 1.0f);
    // Exact end time
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(2.0f, Constant, Constant, defValue), 3.0f);
}

TEST_F(AnimationSequenceTests, LinearInterpolation) {
    float const defValue = 0.0f;

    // Interpolation between first and second key
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(0.5f, Linear, Linear, defValue), 1.5f);
    // Interpolation between second and third key
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(1.5f, Linear, Linear, defValue), 2.5f);
}

TEST_F(AnimationSequenceTests, QuaternionInterpolation) {
    AnimationSequence<float4> quatSeq;
    quatSeq.push_back({.time = 0.0f, .value = {0.0f, 0.0f, 0.0f, 1.0f}});
    quatSeq.push_back({.time = 1.0f, .value = {0.0f, 0.0f, 1.0f, 0.0f}});

    float4 const defValue = {0.0f, 0.0f, 0.0f, 1.0f};

    // Interpolation between quaternions
    auto result = quatSeq.getAtTime<true>(0.5f, Linear, Linear, defValue);
    EXPECT_NEAR(result.x, 0.0f, 1e-5);
    EXPECT_NEAR(result.y, 0.0f, 1e-5);
    EXPECT_NEAR(result.z, 0.7071f, 1e-4);
    EXPECT_NEAR(result.w, 0.7071f, 1e-4);
}

TEST_F(AnimationSequenceTests, EdgeCases) {
    AnimationSequence<float> negativeTimeSeq;
    negativeTimeSeq.push_back({-1.0f, -1.0f});
    negativeTimeSeq.push_back({0.0f, 0.0f});
    negativeTimeSeq.push_back({1.0f, 1.0f});

    float const defValue = 0.0f;

    EXPECT_FLOAT_EQ(negativeTimeSeq.getAtTime(-1.0f, Constant, Constant, defValue), -1.0f);
    EXPECT_FLOAT_EQ(negativeTimeSeq.getAtTime(-0.5f, Linear, Linear, defValue), -0.5f);

    AnimationSequence<float> duplicateTimeSeq;
    duplicateTimeSeq.push_back({0.0f, 1.0f});
    duplicateTimeSeq.push_back({0.0f, 2.0f});
    auto val = duplicateTimeSeq.getAtTime(0.0f, Constant, Constant, defValue);
    EXPECT_TRUE(val == 1.0f || val == 2.0f);

    AnimationSequence<float> largeSeq;
    for (int i = 0; i < 10000; ++i)
        largeSeq.push_back({static_cast<float>(i), static_cast<float>(i)});
    EXPECT_FLOAT_EQ(largeSeq.getAtTime(9999.0f, Constant, Constant, defValue), 9999.0f);
}

TEST_F(AnimationSequenceTests, DifferentBehaviours) {
    float const defValue = 0.0f;

    // Default behaviour
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(-1.0f, Default, Default, defValue), defValue);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(3.0f, Default, Default, defValue), defValue);

    // Constant behaviour
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(-1.0f, Constant, Constant, defValue), 1.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(3.0f, Constant, Constant, defValue), 3.0f);

    // Linear behaviour
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(-1.0f, Linear, Linear, defValue), 1.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(3.0f, Linear, Linear, defValue), 3.0f);

    // Repeat behaviour
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(-1.0f, Repeat, Repeat, defValue), 2.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(3.0f, Repeat, Repeat, defValue), 2.0f);

    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(-1.0f, Constant, Repeat, defValue), 1.0f);
    EXPECT_FLOAT_EQ(multiEltSeq.getAtTime(3.0f, Repeat, Constant, defValue), 3.0f);
}
