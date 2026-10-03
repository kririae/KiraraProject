#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <utility>

#include "TestUtils.h"
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/OptixUtils.h"

namespace {
/// Calls a function when it is destroyed.
class DestructionNotifier {
public:
    explicit DestructionNotifier(std::function<void()> onDestroy)
        : onDestroy_(std::move(onDestroy)) {}
    DestructionNotifier(DestructionNotifier const &) = delete;
    DestructionNotifier &operator=(DestructionNotifier const &) = delete;
    ~DestructionNotifier() { onDestroy_(); }

private:
    std::function<void()> onDestroy_;
};

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
    [[nodiscard]] static std::shared_ptr<DestructionNotifier>
    makeDestructionNotifier(bool &destroyed) {
        return std::make_shared<DestructionNotifier>([&destroyed] { destroyed = true; });
    }

    cudaStream_t stream_{};
};
} // namespace

TEST_F(DeletionQueueTests, DeletesAnObjectOnlyAfterItsWorkFinishes) {
    auto destroyed = false;
    flux::DeletionQueue deletionQueue(stream_);

    {
        flux::test::StreamGate gate(stream_);
        deletionQueue.deleteLater(makeDestructionNotifier(destroyed));
        deletionQueue.deleteFinished();
        EXPECT_FALSE(destroyed);

        gate.open();
        flux::cudaCheck(cudaStreamSynchronize(stream_));
    }
    EXPECT_FALSE(destroyed);

    deletionQueue.deleteFinished();
    EXPECT_TRUE(destroyed);
}

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

    deletionQueue.deleteFinished();
    EXPECT_TRUE(second);
}
