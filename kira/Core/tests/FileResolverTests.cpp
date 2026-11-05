#include <gtest/gtest.h>

#include <filesystem>

#include "kira/FileResolver.h"

TEST(FileResolverTests, AnEmptyPathResolvesToItself) {
    kira::FileResolver const resolver;

    EXPECT_EQ(resolver.resolve({}), std::filesystem::path{});
}
