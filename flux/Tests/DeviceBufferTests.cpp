#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"

static_assert(!std::copy_constructible<flux::DeviceBuffer<std::uint32_t>>);
static_assert(std::movable<flux::DeviceBuffer<std::uint32_t>>);

TEST(DeviceBufferTests, CopiesAndZerosStorageOnItsBoundStream) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    std::array<std::uint32_t, 4> const input{1, 2, 3, 4};
    std::array<std::uint32_t, 4> output{};

    {
        flux::DeviceBuffer<std::uint32_t> buffer(cudaStreamPerThread);
        buffer.copyFromHost({input.data(), input.size()});
        buffer.copyToHost({output.data(), output.size()});
        flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

        EXPECT_EQ(output, input);
        EXPECT_EQ(buffer.size(), input.size());
        EXPECT_EQ(buffer.getStream(), cudaStreamPerThread);

        buffer.zero();
        buffer.copyToHost({output.data(), output.size()});
        flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
        EXPECT_EQ(output, (std::array<std::uint32_t, 4>{}));
    }

    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}

TEST(DeviceBufferTests, SameSizeResizeIsANoOp) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::DeviceBuffer<std::uint32_t> buffer(cudaStreamPerThread);
    buffer.resize(4);
    auto *const allocation = buffer.data();

    buffer.setStream(cudaStreamLegacy);
    EXPECT_EQ(buffer.getStream(), cudaStreamLegacy);
    buffer.setStream(cudaStreamPerThread);
    buffer.resize(4, cudaStreamLegacy);

    EXPECT_EQ(buffer.data(), allocation);
    EXPECT_EQ(buffer.size(), 4);
    EXPECT_EQ(buffer.getStream(), cudaStreamPerThread);

    buffer.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}

TEST(DeviceBufferTests, ExplicitOperationsPublishTheirLifetimeStream) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    std::array<std::uint32_t, 2> const input{1, 2};
    std::array<std::uint32_t, 3> const replacement{3, 4, 5};
    std::array<std::uint32_t, 2> output{};
    flux::DeviceBuffer<std::uint32_t> buffer(cudaStreamPerThread);
    buffer.resize(input.size());
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    buffer.copyFromHost({input.data(), input.size()}, cudaStreamLegacy);
    EXPECT_EQ(buffer.getStream(), cudaStreamLegacy);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamLegacy));

    buffer.copyToHost({output.data(), output.size()}, cudaStreamPerThread);
    EXPECT_EQ(buffer.getStream(), cudaStreamPerThread);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
    EXPECT_EQ(output, input);

    buffer.copyFromHost({replacement.data(), replacement.size()}, cudaStreamLegacy);
    EXPECT_EQ(buffer.getStream(), cudaStreamLegacy);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamLegacy));

    buffer.zero(cudaStreamPerThread);
    EXPECT_EQ(buffer.getStream(), cudaStreamPerThread);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    std::array<std::uint32_t, 3> replacementOutput{};
    buffer.copyToHost({replacementOutput.data(), replacementOutput.size()}, cudaStreamLegacy);
    EXPECT_EQ(buffer.getStream(), cudaStreamLegacy);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamLegacy));
    EXPECT_EQ(replacementOutput, (std::array<std::uint32_t, 3>{}));

    buffer.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamLegacy));
}

TEST(DeviceBufferTests, RejectedResizePreservesTheAllocation) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::DeviceBuffer<std::uint64_t> buffer(cudaStreamPerThread);
    buffer.resize(2);
    auto *const allocation = buffer.data();
    constexpr auto impossibleCount =
        std::numeric_limits<std::size_t>::max() / sizeof(std::uint64_t) + 1;

    EXPECT_THROW(buffer.resize(impossibleCount), std::invalid_argument);
    EXPECT_EQ(buffer.data(), allocation);
    EXPECT_EQ(buffer.size(), 2);

    buffer.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}

TEST(DeviceBufferTests, MoveTransfersTheAllocationAndStream) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    {
        flux::DeviceBuffer<std::uint32_t> source(cudaStreamPerThread);
        source.resize(4);
        auto *const allocation = source.data();

        flux::DeviceBuffer<std::uint32_t> moved(std::move(source));
        EXPECT_EQ(moved.data(), allocation);
        EXPECT_EQ(moved.getStream(), cudaStreamPerThread);

        flux::DeviceBuffer<std::uint32_t> target;
        target.resize(1);
        target = std::move(moved);
        EXPECT_EQ(target.data(), allocation);
        EXPECT_EQ(target.size(), 4);
        EXPECT_EQ(target.getStream(), cudaStreamPerThread);
    }

    flux::cudaCheck(cudaDeviceSynchronize());
}

TEST(DeviceBufferTests, CopyToHostRequiresMatchingStorage) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::DeviceBuffer<std::uint32_t> buffer(cudaStreamPerThread);
    buffer.resize(2);
    std::array<std::uint32_t, 1> output{};

    EXPECT_THROW(buffer.copyToHost({output.data(), output.size()}), std::invalid_argument);

    buffer.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}
