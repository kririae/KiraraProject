#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <utility>

#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

TEST(OptixPipelineTests, LaunchesRaygenProgram) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("width", std::uint32_t{1});
    properties.set("height", std::uint32_t{1});
    auto product = flux::RenderProduct::create(camera, std::move(properties));
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    EXPECT_EQ(handler.getContext(), context);
    EXPECT_THROW(handler.render(*product, 0), std::invalid_argument);
    EXPECT_NO_THROW(handler.render(*product, 4));
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 4);

    handler.setSampleOffset(0);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 4);
    handler.setSampleOffset(100);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_NO_THROW(handler.render(*product, 2));
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);

    product->getFilm().setResolution(2, 2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_NO_THROW(handler.render(*product, 1));
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 1);

    handler.release(*product);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
}

TEST(OptixPipelineTests, ReleasesStateAfterConstructionFails) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    EXPECT_THROW((void)flux::OptixHandler(context, std::filesystem::path{}), kira::Anyhow);
    EXPECT_EQ(context.getRefCount(), 1);

    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("width", std::uint32_t{1});
    properties.set("height", std::uint32_t{1});
    auto product = flux::RenderProduct::create(camera, std::move(properties));
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.render(*product, 1));
}

TEST(OptixPipelineTests, InvalidatesOnlyTheChangedRenderTarget) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    auto firstCamera = flux::Camera::create();
    auto secondCamera = flux::Camera::create();

    kira::Properties firstProperties;
    firstProperties.set("width", std::uint32_t{1});
    firstProperties.set("height", std::uint32_t{1});
    auto firstProduct = flux::RenderProduct::create(firstCamera, std::move(firstProperties));

    kira::Properties secondProperties;
    secondProperties.set("width", std::uint32_t{1});
    secondProperties.set("height", std::uint32_t{1});
    auto secondProduct = flux::RenderProduct::create(secondCamera, std::move(secondProperties));

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    handler.render(*firstProduct, 1);
    handler.render(*secondProduct, 1);

    firstCamera->setPosition({0.0F, 0.0F, 1.0F});
    EXPECT_EQ(handler.getAccumulatedSamples(*firstProduct), 0);
    EXPECT_EQ(handler.getAccumulatedSamples(*secondProduct), 1);
}
