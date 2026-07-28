#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <utility>

#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

TEST(OptixPipelineTests, UsesAStableProgramTypeSbtLayout) {
    EXPECT_EQ(
        flux::OptixSbt::getHitgroupRecord(
            flux::BSDFType::Diffuse, flux::OptixGeometryType::Triangle, flux::RayType::Radiance
        ),
        0
    );
    EXPECT_EQ(
        flux::OptixSbt::getHitgroupRecord(
            flux::BSDFType::Diffuse, flux::OptixGeometryType::Triangle, flux::RayType::Shadow
        ),
        1
    );
    EXPECT_EQ(
        flux::OptixSbt::getInstanceOffset(
            flux::BSDFType::Diffuse, flux::OptixGeometryType::Triangle
        ),
        0
    );
    EXPECT_EQ(flux::OptixSbt::getNumHitgroupRecords(), 2);
}

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

TEST(OptixPipelineTests, InvalidatesAffectedRenderProducts) {
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
    EXPECT_TRUE(handler.isConverged(*firstProduct));
    EXPECT_TRUE(handler.isConverged(*secondProduct));

    firstCamera->setPosition({0.0F, 0.0F, 1.0F});
    EXPECT_EQ(handler.getAccumulatedSamples(*firstProduct), 0);
    EXPECT_EQ(handler.getAccumulatedSamples(*secondProduct), 1);
    EXPECT_FALSE(handler.isConverged(*firstProduct));
    EXPECT_TRUE(handler.isConverged(*secondProduct));

    handler.render(*firstProduct, 1);
    handler.sync();
    EXPECT_EQ(handler.getAccumulatedSamples(*firstProduct), 0);
    EXPECT_EQ(handler.getAccumulatedSamples(*secondProduct), 0);
    EXPECT_FALSE(handler.isConverged(*firstProduct));
    EXPECT_FALSE(handler.isConverged(*secondProduct));
}

TEST(OptixPipelineTests, TracksFilmLayoutAndConvergence) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("width", std::uint32_t{2});
    properties.set("height", std::uint32_t{2});
    properties.set("num_samples", std::uint32_t{4});
    auto product = flux::RenderProduct::create(camera, std::move(properties));
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_FALSE(handler.isConverged(*product));

    handler.render(*product, 2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);
    EXPECT_FALSE(handler.isConverged(*product));

    product->getFilm().setChannels(flux::FilmChannels::Normal);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    handler.render(*product, 2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);

    product->getFilm().setChannels(flux::FilmChannels::Normal);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);

    product->getFilm().setChannels(flux::FilmChannels::All);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    handler.render(*product, 2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);

    product->setSamplesPerPixel(2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);
    EXPECT_TRUE(handler.isConverged(*product));

    product->setSamplesPerPixel(8);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);
    EXPECT_FALSE(handler.isConverged(*product));

    product->getFilm().setResolution(2, 2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);

    product->getFilm().setResolution(1, 4);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_FALSE(handler.isConverged(*product));
    handler.render(*product, 3);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 3);

    product->getFilm().setResolution(1, 1);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    handler.render(*product, 8);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 8);
    EXPECT_TRUE(handler.isConverged(*product));

    product->getFilm().setResolution(2, 2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_FALSE(handler.isConverged(*product));
    handler.render(*product, 9);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 9);
    EXPECT_TRUE(handler.isConverged(*product));
}
