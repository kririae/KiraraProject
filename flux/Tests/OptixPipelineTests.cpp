#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

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
        flux::OptixSbt::getHitgroupBlock(flux::BSDFType::Diffuse, flux::GeometryType::TriangleMesh),
        0
    );
    EXPECT_EQ(
        flux::OptixSbt::getInstanceOffset(
            flux::BSDFType::Diffuse, flux::GeometryType::TriangleMesh
        ),
        0
    );
    EXPECT_EQ(flux::OptixSbt::getNumHitgroupRecords(), 1);
}

TEST(OptixPipelineTests, RendersAndDownloadsFilmChannels) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, properties);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    EXPECT_EQ(handler.getContext(), context);
    EXPECT_THROW(handler.download(*product), kira::Anyhow);
    EXPECT_THROW(handler.render(*product, 0), std::invalid_argument);
    EXPECT_NO_THROW(handler.render(*product, 4));
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 4);
    EXPECT_NO_THROW(handler.download(*product));
    ASSERT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 1);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>()[0], (flux::Vec3f{}));
    ASSERT_EQ(product->getFilm().getChannel<flux::AlbedoChannel>().size(), 1);
    EXPECT_EQ(product->getFilm().getChannel<flux::AlbedoChannel>()[0], (flux::Vec3f{}));

    handler.setSampleOffset(0);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 4);
    handler.setSampleOffset(100);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 1);
    EXPECT_NO_THROW(handler.render(*product, 2));
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);

    handler.release(*product);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
}

TEST(OptixPipelineTests, ReleasesContextAfterConstructionFails) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    EXPECT_THROW((void)flux::OptixHandler(context, std::filesystem::path{}), kira::Anyhow);
    EXPECT_EQ(context.getRefCount(), 1);

    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, properties);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.render(*product, 1));
}

TEST(OptixPipelineTests, InvalidatesAccumulationAfterCameraChangeAndSync) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    auto firstCamera = flux::Camera::create();
    auto secondCamera = flux::Camera::create();

    kira::Properties firstProperties;
    firstProperties.set("resolution", flux::Vec2u{1, 1});
    auto firstProduct = flux::RenderProduct::create(firstCamera, firstProperties);

    kira::Properties secondProperties;
    secondProperties.set("resolution", flux::Vec2u{1, 1});
    auto secondProduct = flux::RenderProduct::create(secondCamera, secondProperties);

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

TEST(OptixPipelineTests, TracksAccumulationAcrossFilmAndSampleTargetChanges) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{2, 2});
    properties.set("num_samples", std::uint32_t{4});
    auto product = flux::RenderProduct::create(camera, properties);
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
