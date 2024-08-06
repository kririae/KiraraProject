#include <gtest/gtest.h>

#include "kira/Assertions.h"

using namespace kira;

TEST(AssertionTests, ForceAssert1) { KIRA_FORCE_ASSERT1(false); }
