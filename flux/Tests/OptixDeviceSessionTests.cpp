#include <cuda.h>
#include <gtest/gtest.h>

#include <memory>

#include "SceneUtils.h"
#include "TestUtils.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixDeviceSession.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "kira/Anyhow.h"

namespace {
[[nodiscard]] CUcontext getCurrentContext() {
    CUcontext current = nullptr;
    flux::cuCheck(cuCtxGetCurrent(&current));
    return current;
}

/// Exercises every public operation of the session, the handler, and the buffer that touches CUDA.
/// After each operation, the current context of the calling thread must be \p expected.
void expectOperationsLeaveCurrentContext(CUcontext expected) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();

    auto session = std::make_unique<flux::OptixDeviceSession>(0);
    EXPECT_EQ(getCurrentContext(), expected);

    auto handler = std::make_unique<flux::OptixHandler>(*session, context);
    EXPECT_EQ(getCurrentContext(), expected);

    handler->sync();
    EXPECT_EQ(getCurrentContext(), expected);

    auto buffer = std::make_unique<flux::OptixAccumulationBuffer>(
        *session, flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::All}
    );
    EXPECT_EQ(getCurrentContext(), expected);

    EXPECT_NO_THROW(handler->render(*camera, *buffer, 2));
    EXPECT_EQ(getCurrentContext(), expected);

    flux::Film film{flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::All}};
    EXPECT_NO_THROW(buffer->download(film));
    EXPECT_EQ(getCurrentContext(), expected);

    buffer->clear();
    EXPECT_NO_THROW(handler->render(*camera, *buffer, 2));
    buffer.reset();
    EXPECT_EQ(getCurrentContext(), expected);

    handler.reset();
    EXPECT_EQ(getCurrentContext(), expected);

    session.reset();
    EXPECT_EQ(getCurrentContext(), expected);
}
} // namespace

TEST(OptixDeviceSessionTests, LeavesNoCurrentContextWhenCallerHadNone) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::cuCheck(cuInit(0));
    flux::cuCheck(cuCtxSetCurrent(nullptr));
    ASSERT_EQ(getCurrentContext(), nullptr);

    expectOperationsLeaveCurrentContext(nullptr);
}

TEST(OptixDeviceSessionTests, RestoresACallerCreatedContext) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::cuCheck(cuInit(0));
    CUdevice device{};
    flux::cuCheck(cuDeviceGet(&device, 0));
    CUcontext callerContext = nullptr;
#if CUDA_VERSION >= 13000
    flux::cuCheck(cuCtxCreate(&callerContext, nullptr, 0, device));
#else
    flux::cuCheck(cuCtxCreate(&callerContext, 0, device));
#endif
    ASSERT_EQ(getCurrentContext(), callerContext);

    expectOperationsLeaveCurrentContext(callerContext);

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
    flux::cuCheck(cuCtxSetCurrent(nullptr));
    EXPECT_THROW((void)flux::OptixDeviceSession(deviceCount), kira::Anyhow);
    EXPECT_THROW((void)flux::OptixDeviceSession(-1), kira::Anyhow);
    EXPECT_EQ(getCurrentContext(), nullptr);
}
