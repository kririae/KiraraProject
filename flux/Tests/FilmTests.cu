#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Film.cuh"

namespace {
struct AccumulateOnePixel {
    flux::Film::DeviceImpl film;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        film.accumulateNormal({0, 0}, {1.0F, 2.0F, 4.0F});
    }
};
} // namespace

TEST(FilmTests, AtomicallyAccumulatesConcurrentSamples) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    constexpr std::size_t sampleCount = 257;
    flux::DeviceBuffer<flux::Vec3f> normal(cudaStreamPerThread);
    normal.resize(1);
    normal.zero();

    auto const film = flux::Film::DeviceImpl{
        .width = 1,
        .height = 1,
        .normal = normal.data(),
    };
    flux::launchLinearKernel(sampleCount, AccumulateOnePixel{.film = film}, cudaStreamPerThread);

    std::array<flux::Vec3f, 1> result{};
    normal.copyToHost(result);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_FLOAT_EQ(result[0].x(), 257.0F);
    EXPECT_FLOAT_EQ(result[0].y(), 514.0F);
    EXPECT_FLOAT_EQ(result[0].z(), 1028.0F);
}
