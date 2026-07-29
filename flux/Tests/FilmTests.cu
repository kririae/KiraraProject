#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/FilmImpl.h"

namespace {
struct AccumulateOnePixel {
    flux::Film::Impl film;

    KIRA_DEVICE void operator()(std::size_t) const noexcept {
        film.accumulate<flux::NormalChannel>({0, 0}, {1.0F, 2.0F, 4.0F});
        film.accumulate<flux::AlbedoChannel>({0, 0}, {2.0F, 3.0F, 5.0F});
    }
};
} // namespace

TEST(FilmTests, AtomicallyAccumulatesConcurrentSamples) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    constexpr std::size_t sampleCount = 257;
    flux::DeviceBuffer<flux::Vec3f> normal(cudaStreamPerThread);
    flux::DeviceBuffer<flux::Vec3f> albedo(cudaStreamPerThread);
    normal.resize(1);
    albedo.resize(1);
    normal.zero();
    albedo.zero();

    auto film = flux::Film::Impl{.width = 1, .height = 1};
    film.channels.get<flux::NormalChannel>().data = normal.data();
    film.channels.get<flux::AlbedoChannel>().data = albedo.data();
    flux::launchLinearKernel(sampleCount, AccumulateOnePixel{.film = film}, cudaStreamPerThread);

    std::array<flux::Vec3f, 1> normalResult{};
    std::array<flux::Vec3f, 1> albedoResult{};
    normal.copyToHost(normalResult);
    albedo.copyToHost(albedoResult);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_FLOAT_EQ(normalResult[0].x(), 257.0F);
    EXPECT_FLOAT_EQ(normalResult[0].y(), 514.0F);
    EXPECT_FLOAT_EQ(normalResult[0].z(), 1028.0F);
    EXPECT_FLOAT_EQ(albedoResult[0].x(), 514.0F);
    EXPECT_FLOAT_EQ(albedoResult[0].y(), 771.0F);
    EXPECT_FLOAT_EQ(albedoResult[0].z(), 1285.0F);
}

TEST(FilmTests, SkipsChannelsMissingFromTheDeviceView) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    flux::DeviceBuffer<flux::Vec3f> normal(cudaStreamPerThread);
    normal.resize(1);
    normal.zero();

    auto film = flux::Film::Impl{.width = 1, .height = 1};
    film.channels.get<flux::NormalChannel>().data = normal.data();
    flux::launchLinearKernel(1, AccumulateOnePixel{.film = film}, cudaStreamPerThread);

    std::array<flux::Vec3f, 1> result{};
    normal.copyToHost(result);
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_TRUE(film.hasChannel<flux::NormalChannel>());
    EXPECT_FALSE(film.hasChannel<flux::AlbedoChannel>());
    EXPECT_EQ(result[0], (flux::Vec3f{1.0F, 2.0F, 4.0F}));
}
