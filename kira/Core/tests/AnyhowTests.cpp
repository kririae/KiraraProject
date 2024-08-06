#include <gtest/gtest.h>

#include "kira/Anyhow.h"

using namespace kira;

// TODO(krr): I might add test for rethrowing exceptions.
TEST(AnyhowTests, ThrowDefaultAnyhow) { EXPECT_THROW(throw Anyhow{}, Anyhow); }

TEST(AnyhowTests, ThrowAnyhow) {
    EXPECT_THROW(throw Anyhow("Something went wrong"), Anyhow);
    EXPECT_THROW(throw Anyhow("Something went wrong: {}", 42), Anyhow);
}

TEST(AnyhowTests, MultipleArguments) {
    try {
        throw Anyhow("Multiple args: {}, {}, {}", 1, "two", 3.0);
    } catch (Anyhow const &e) { EXPECT_STREQ("Multiple args: 1, two, 3", e.what()); }
}

TEST(AnyhowTests, ExceptionMessage) {
    try {
        throw Anyhow("Test message");
    } catch (Anyhow const &e) { EXPECT_STREQ("Test message", e.what()); }

    try {
        throw Anyhow("Formatted message: {}", 42);
    } catch (Anyhow const &e) { EXPECT_STREQ("Formatted message: 42", e.what()); }
}

TEST(AnyhowTests, CaptureStderr) {
    ::testing::internal::CaptureStderr();
    EXPECT_THROW(throw Anyhow("Something went wrong"), Anyhow);
    EXPECT_THROW(throw Anyhow("Something went wrong again"), Anyhow);
    auto const &output = ::testing::internal::GetCapturedStderr();
    EXPECT_TRUE(output.find("Something went wrong") == std::string::npos);
    EXPECT_TRUE(output.find("Something went wrong again") == std::string::npos);
}
