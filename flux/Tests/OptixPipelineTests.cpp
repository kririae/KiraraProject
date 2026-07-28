#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
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
    auto camera = context->create<flux::Camera>();
    kira::Properties properties;
    properties.set("width", std::uint32_t{1});
    properties.set("height", std::uint32_t{1});
    auto product = context->create<flux::RenderProduct>(std::move(properties));
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    EXPECT_EQ(handler.getContext(), context);
    EXPECT_NO_THROW(handler.render(*camera, *product, 0));

    product->getFilm().setResolution(2, 2);
    EXPECT_NO_THROW(handler.render(*camera, *product, 1));

    auto foreignContext = flux::Context::create();
    auto foreignCamera = foreignContext->create<flux::Camera>();
    EXPECT_THROW(handler.render(*foreignCamera, *product, 0), std::invalid_argument);
}

TEST(OptixPipelineTests, ReleasesStateAfterConstructionFails) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    EXPECT_THROW((void)flux::OptixHandler(context, std::filesystem::path{}), kira::Anyhow);
    EXPECT_EQ(context.getRefCount(), 1);

    auto camera = context->create<flux::Camera>();
    kira::Properties properties;
    properties.set("width", std::uint32_t{1});
    properties.set("height", std::uint32_t{1});
    auto product = context->create<flux::RenderProduct>(std::move(properties));
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.render(*camera, *product, 0));
}
