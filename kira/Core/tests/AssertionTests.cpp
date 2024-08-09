#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "kira/Assertions.h"

using namespace kira;

using ::testing::HasSubstr;
using ::testing::StartsWith;

TEST(AssertionTests, ForceAssert) {
    EXPECT_DEATH_IF_SUPPORTED(KIRA_FORCE_ASSERT(false), StartsWith("Assertion (false) failed at"));
    EXPECT_DEATH_IF_SUPPORTED(
        KIRA_FORCE_ASSERT(false, "Because of cute girls"), HasSubstr("Because of cute girls")
    );
    EXPECT_DEATH_IF_SUPPORTED(
        KIRA_FORCE_ASSERT(false, "Because of {}", 42), HasSubstr("Because of 42")
    );
}

TEST(AssertionTests, Assert) {
    EXPECT_DEBUG_DEATH(KIRA_ASSERT(false), StartsWith("Assertion (false) failed at"));
    EXPECT_DEBUG_DEATH(
        KIRA_ASSERT(false, "Because of cute girls"), HasSubstr("Because of cute girls")
    );
    EXPECT_DEBUG_DEATH(KIRA_ASSERT(false, "Because of {}", 42), HasSubstr("Because of 42"));
}
