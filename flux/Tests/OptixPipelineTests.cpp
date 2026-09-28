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
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

TEST(OptixPipelineTests, UsesAStableProgramTypeSbtLayout) {
    EXPECT_EQ(
        flux::OptixSbt::getHitgroupRecordIndex(
            flux::BSDFType::Diffuse, flux::GeometryType::TriangleMesh
        ),
        0
    );
    EXPECT_EQ(
        flux::OptixSbt::getInstanceOffset(
            flux::BSDFType::Diffuse, flux::GeometryType::TriangleMesh
        ),
        0
    );
    EXPECT_EQ(
        flux::OptixSbt::getHitgroupRecordIndex(
            flux::BSDFType::Principled, flux::GeometryType::TriangleMesh
        ),
        1
    );
    EXPECT_EQ(
        flux::OptixSbt::getInstanceOffset(
            flux::BSDFType::Principled, flux::GeometryType::TriangleMesh
        ),
        1
    );
    EXPECT_EQ(flux::OptixSbt::getNumHitgroupRecords(), 2);
}

TEST(OptixPipelineTests, RendersAndDownloadsFilmChannels) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
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
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    EXPECT_THROW((void)flux::OptixHandler(context, std::filesystem::path{}), kira::Anyhow);
    EXPECT_EQ(context.getRefCount(), 1);

    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, properties);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.render(*product, 1));
}

TEST(OptixPipelineTests, RendersConstantEnvironmentMapOnMiss) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    kira::Properties lightProps;
    lightProps.set("scale", flux::Spectrum{0.25F, 0.5F, 0.75F});
    (void)context->create<flux::EnvMapLight>(lightProps);

    auto camera = flux::Camera::create();
    kira::Properties productProps;
    productProps.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, productProps);
    product->getFilm().setChannels(flux::FilmChannels::Color);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    handler.render(*product, 1);
    handler.download(*product);

    auto const color = product->getFilm().getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 1);
    EXPECT_EQ(color.front(), (flux::Spectrum{0.25F, 0.5F, 0.75F}));
}

TEST(OptixPipelineTests, RendersImageEnvironmentMapOnMiss) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    kira::Properties textureProps;
    textureProps.set("type", "image");
    textureProps.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Texture2x2.ppm");
    kira::Properties lightProps;
    lightProps.set("texture", textureProps);
    (void)context->create<flux::EnvMapLight>(lightProps);

    kira::Properties cameraProps;
    cameraProps.set("fov", 1.0e-4F);
    auto camera = flux::Camera::create(cameraProps);
    kira::Properties productProps;
    productProps.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, productProps);
    product->getFilm().setChannels(flux::FilmChannels::Color);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    handler.render(*product, 1);
    handler.download(*product);

    auto const color = product->getFilm().getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 1);
    EXPECT_NEAR(color.front().x(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.front().y(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.front().z(), 0.5F, 1.0e-5F);
}

TEST(OptixPipelineTests, InvalidatesAccumulationAfterCameraChangeAndSync) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
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
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
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
