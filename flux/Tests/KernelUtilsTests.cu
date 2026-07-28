#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "kira/Compiler.h"

namespace {
struct StoreLinearIndex {
    std::size_t *output;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept { output[index] = index; }
};
} // namespace

TEST(KernelUtilsTests, EmptyRangeDoesNotLaunch) {
    EXPECT_NO_THROW(
        flux::launchLinearKernel(
            0, StoreLinearIndex{.output = nullptr}, reinterpret_cast<cudaStream_t>(1)
        )
    );
}

TEST(KernelUtilsTests, AppliesFunctorToEveryLinearIndex) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    constexpr std::size_t elementCount = 257;
    flux::DeviceBuffer<std::size_t> output(cudaStreamPerThread);
    output.resize(elementCount);

    flux::launchLinearKernel(
        elementCount, StoreLinearIndex{.output = output.data()}, cudaStreamPerThread
    );

    std::array<std::size_t, elementCount> hostOutput{};
    output.copyToHost(hostOutput);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
    for (std::size_t index = 0; index < hostOutput.size(); ++index)
        EXPECT_EQ(hostOutput[index], index);
}

TEST(KernelUtilsTests, RejectsUnsupportedLaunchSize) {
    EXPECT_THROW(
        flux::launchLinearKernel(
            std::numeric_limits<std::size_t>::max(), StoreLinearIndex{.output = nullptr},
            cudaStreamPerThread
        ),
        std::invalid_argument
    );
}
