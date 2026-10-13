#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "TestUtils.h"
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/OptixUtils.h"

namespace {
class DeletionQueueTests : public testing::Test {
protected:
    void SetUp() override {
        if (!flux::test::hasCudaDevice())
            GTEST_SKIP() << "No CUDA device is available";
        flux::cudaCheck(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
    }

    void TearDown() override {
        if (stream_)
            flux::cudaCheck<false>(cudaStreamDestroy(stream_));
    }

    /// Returns an object that sets \p destroyed to true when it is destroyed.
    [[nodiscard]] static std::shared_ptr<void> makeDestructionNotifier(bool &destroyed) {
        return {&destroyed, [](bool *flag) { *flag = true; }};
    }

    cudaStream_t stream_{};
};
} // namespace

TEST_F(DeletionQueueTests, DeletesOnlyTheObjectsWhoseWorkHasFinished) {
    auto first = false;
    auto second = false;
    flux::DeletionQueue deletionQueue(stream_);

    deletionQueue.deleteLater(makeDestructionNotifier(first));
    deletionQueue.deleteFinished();
    flux::cudaCheck(cudaStreamSynchronize(stream_));

    {
        flux::test::StreamGate gate(stream_);
        deletionQueue.deleteLater(makeDestructionNotifier(second));
        deletionQueue.deleteFinished();
        EXPECT_TRUE(first);
        EXPECT_FALSE(second);

        gate.open();
        flux::cudaCheck(cudaStreamSynchronize(stream_));
    }
    EXPECT_FALSE(second);

    deletionQueue.deleteFinished();
    EXPECT_TRUE(second);
}
