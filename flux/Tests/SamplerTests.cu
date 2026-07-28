#include <cuda_runtime_api.h>
#include <gtest/gtest.h>

#include <stdexcept>
#include <utility>

#include "TestUtils.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Sampler.cuh"
#include "flux/Scene/Context.h"
#include "flux/Scene/TXContext.h"
#include "kira/Anyhow.h"

namespace {
class ThrowingSamplerOwner final : public flux::RenderObject {
    friend class flux::TXContext;

    ThrowingSamplerOwner(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        (void)tx.create<flux::IndependentSampler>();
        throw std::runtime_error("intentional sampler transaction failure");
    }
};

struct SamplerResult {
    flux::Vec2f first;
    flux::Vec2f repeated;
    flux::Vec2f differentSample;
    flux::Vec2f differentPixel;
    float next;
    flux::SamplerType type;
};

__global__ void sampleIndependentSampler(flux::Sampler::DeviceImpl sampler, SamplerResult *result) {
    auto const pixel = flux::Vec2u{3U, 2U};
    auto const resolution = flux::Vec2u{16U, 9U};

    sampler.startPixelSample(pixel, 7U, resolution);
    result->first = sampler.get2D();
    result->next = sampler.get1D();

    sampler.startPixelSample(pixel, 7U, resolution);
    result->repeated = sampler.get2D();
    result->type = sampler.type;

    sampler.startPixelSample(pixel, 8U, resolution);
    result->differentSample = sampler.get2D();

    sampler.startPixelSample({4U, 2U}, 7U, resolution);
    result->differentPixel = sampler.get2D();
}
} // namespace

TEST(SamplerTests, KeepsFirstSuccessfulSamplerActive) {
    auto context = flux::Context::create();

    EXPECT_THROW((void)context->getActiveSampler(), kira::Anyhow);
    EXPECT_THROW((void)context->create<ThrowingSamplerOwner>(), std::runtime_error);
    EXPECT_THROW((void)context->getActiveSampler(), kira::Anyhow);

    auto first = context->create<flux::IndependentSampler>();
    EXPECT_EQ(context->getActiveSampler().get(), first.get());

    (void)context->create<flux::IndependentSampler>();
    EXPECT_EQ(context->getActiveSampler().get(), first.get());
}

TEST(SamplerTests, DispatchesDeterministicPixelSequences) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::IndependentSampler>();
    auto const resolution = flux::Vec2u{16U, 9U};
    flux::DeviceBuffer<SamplerResult> deviceResult(cudaStreamPerThread);
    deviceResult.resize(1);
    sampleIndependentSampler<<<1, 1, 0, cudaStreamPerThread>>>(
        context->getActiveSampler()->getDeviceImpl(resolution), deviceResult.data()
    );
    flux::cudaCheck(cudaGetLastError());

    SamplerResult result{};
    deviceResult.copyToHost({&result, 1});
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));

    EXPECT_EQ(result.type, flux::SamplerType::Independent);
    EXPECT_FLOAT_EQ(result.first.x(), result.repeated.x());
    EXPECT_FLOAT_EQ(result.first.y(), result.repeated.y());
    EXPECT_GE(result.first.x(), 0.0F);
    EXPECT_LT(result.first.x(), 1.0F);
    EXPECT_GE(result.first.y(), 0.0F);
    EXPECT_LT(result.first.y(), 1.0F);
    EXPECT_GE(result.next, 0.0F);
    EXPECT_LT(result.next, 1.0F);
    EXPECT_NE(result.first.x(), result.differentSample.x());
    EXPECT_NE(result.first.x(), result.differentPixel.x());

    deviceResult.clear();
    flux::cudaCheck(cudaStreamSynchronize(cudaStreamPerThread));
}
