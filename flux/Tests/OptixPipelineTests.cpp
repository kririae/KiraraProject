#include <gtest/gtest.h>

#include <filesystem>

#include "TestUtils.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Context.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

TEST(OptixPipelineTests, LaunchesRaygenProgram) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    EXPECT_EQ(handler.getContext(), context);
    EXPECT_NO_THROW(handler.launch());
}

TEST(OptixPipelineTests, ReleasesStateAfterConstructionFails) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    EXPECT_THROW((void)flux::OptixHandler(context, std::filesystem::path{}), kira::Anyhow);
    EXPECT_EQ(context.getRefCount(), 1);

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.launch());
}
