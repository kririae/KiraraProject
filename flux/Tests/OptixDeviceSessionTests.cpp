#include <cuda.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include "SceneUtils.h"
#include "TestUtils.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

namespace {
[[nodiscard]] CUcontext getCurrentContext() {
    CUcontext current = nullptr;
    flux::cuCheck(cuCtxGetCurrent(&current));
    return current;
}

/// Exercises every public handler operation that touches CUDA, expecting the calling thread's
/// current context to be \p expected after each of them.
void expectHandlerLeavesCurrentContext(CUcontext expected) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, properties);

    auto handler =
        std::make_unique<flux::OptixHandler>(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_EQ(getCurrentContext(), expected);

    handler->sync();
    EXPECT_EQ(getCurrentContext(), expected);

    EXPECT_NO_THROW(handler->render(*product, 2));
    EXPECT_EQ(getCurrentContext(), expected);

    EXPECT_NO_THROW(handler->download(*product));
    EXPECT_EQ(getCurrentContext(), expected);

    handler->release(*product);
    EXPECT_EQ(getCurrentContext(), expected);

    EXPECT_NO_THROW(handler->render(*product, 2));
    handler.reset();
    EXPECT_EQ(getCurrentContext(), expected);
}
} // namespace

TEST(OptixDeviceSessionTests, LeavesNoCurrentContextWhenCallerHadNone) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::cuCheck(cuInit(0));
    flux::cuCheck(cuCtxSetCurrent(nullptr));
    ASSERT_EQ(getCurrentContext(), nullptr);

    expectHandlerLeavesCurrentContext(nullptr);
}

TEST(OptixDeviceSessionTests, RestoresACallerCreatedContext) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::cuCheck(cuInit(0));
    CUdevice device{};
    flux::cuCheck(cuDeviceGet(&device, 0));
    CUcontext callerContext = nullptr;
    flux::cuCheck(cuCtxCreate(&callerContext, nullptr, 0, device));
    ASSERT_EQ(getCurrentContext(), callerContext);

    expectHandlerLeavesCurrentContext(callerContext);

    // The handler never owned the caller's context, so it is still valid and current here.
    EXPECT_EQ(getCurrentContext(), callerContext);
    flux::cuCheck(cuCtxDestroy(callerContext));
    flux::cuCheck(cuCtxSetCurrent(nullptr));
}

TEST(OptixDeviceSessionTests, RejectsADeviceOrdinalOutsideTheDeviceRange) {
    if (!flux::test::hasCudaDevice())
        GTEST_SKIP() << "No CUDA device is available";

    int deviceCount = 0;
    flux::cuCheck(cuInit(0));
    flux::cuCheck(cuDeviceGetCount(&deviceCount));
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto const modulePath = std::filesystem::path(FLUX_TEST_OPTIX_IR);

    flux::cuCheck(cuCtxSetCurrent(nullptr));
    EXPECT_THROW((void)flux::OptixHandler(context, modulePath, deviceCount), kira::Anyhow);
    EXPECT_THROW((void)flux::OptixHandler(context, modulePath, -1), kira::Anyhow);
    EXPECT_EQ(getCurrentContext(), nullptr);
    EXPECT_EQ(context.getRefCount(), 1);
}
