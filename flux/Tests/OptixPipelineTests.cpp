#include <gtest/gtest.h>

#include <array>
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
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

namespace {
[[nodiscard]] kira::Properties
primitiveProperties(flux::TriangleMesh const &mesh, flux::BSDF const &bsdf) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh.getContextId()));
    properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf.getContextId()));
    return properties;
}

[[nodiscard]] flux::TriangleMesh::Data triangleData() {
    return {
        .vertices = flux::test::sharedBuffer(
            flux::Vec3f{0.0F, 0.0F, 0.0F}, flux::Vec3f{1.0F, 0.0F, 0.0F},
            flux::Vec3f{0.0F, 1.0F, 0.0F}
        ),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
    };
}
} // namespace

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

TEST(OptixPipelineTests, RendersWithIndicesThatFollowUnusedObjectsAndAfterAResync) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    // Light a triangle with an emitter and return the center pixel.
    auto const render = [&](bool withUnusedMesh, bool withHiddenPrimitive, bool withResync) {
        auto context = flux::Context::create();
        (void)context->create<flux::PathIntegrator>(kira::Properties{});
        (void)context->create<flux::IndependentSampler>(kira::Properties{});

        // Create a mesh first that no primitive references, so the visible mesh has index 1.
        if (withUnusedMesh)
            (void)context->create<flux::TriangleMesh>(triangleData());
        auto mesh = context->create<flux::TriangleMesh>(triangleData());
        auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});

        // Create a hidden primitive first, so the visible primitive has index 1.
        if (withHiddenPrimitive)
            context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf))->setVisible(false);
        auto primitive = context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
        EXPECT_EQ(
            context->getIndex<flux::Primitive>(primitive->getContextId()),
            withHiddenPrimitive ? 1 : 0
        );
        EXPECT_EQ(context->getIndex<flux::Geometry>(mesh->getContextId()), withUnusedMesh ? 1 : 0);

        // Add an emitter facing the triangle, which light sampling reaches by primitive index.
        auto edf = context->create<flux::ConstantEDF>(kira::Properties{});
        auto emitterProperties = primitiveProperties(*mesh, *bsdf);
        emitterProperties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
        auto emitter = context->create<flux::Primitive>(emitterProperties);
        emitter->setTransform(
            {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 1.0F}
        );

        // Add a second emitter that the re-sync hides again.
        auto extra = context->create<flux::Primitive>(emitterProperties);
        extra->setTransform(
            {1.0F, 0.0F, 0.0F, 0.5F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 1.0F}
        );
        extra->setVisible(withResync);

        kira::Properties cameraProperties;
        cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
        cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
        cameraProperties.set("fov", 1.0F);
        auto camera = flux::Camera::create(cameraProperties);
        kira::Properties productProperties;
        productProperties.set("resolution", flux::Vec2u{1, 1});
        productProperties.set("num_samples", std::uint32_t{4});
        auto product = flux::RenderProduct::create(camera, productProperties);
        product->getFilm().setChannels(flux::FilmChannels::Color);

        flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
        auto const color = [&] {
            handler.render(*product, 4);
            handler.download(*product);
            return product->getFilm().getChannel<flux::ColorChannel>()[0];
        };
        if (!withResync)
            return color();

        // Render with the second emitter, then hide it and sync the same handler.
        auto const lit = color();
        extra->setVisible(false);
        handler.sync();
        auto const dimmed = color();
        EXPECT_NE(dimmed, lit);
        return dimmed;
    };

    auto const expected = render(false, false, false);
    EXPECT_GT(expected.x(), 0.0F);
    EXPECT_EQ(render(true, false, false), expected);
    EXPECT_EQ(render(false, true, false), expected);
    EXPECT_EQ(render(false, false, true), expected);
}

TEST(OptixPipelineTests, HoldsNoContextObjectAfterSync) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});
    auto properties = primitiveProperties(*mesh, *bsdf);
    properties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    auto primitive = context->create<flux::Primitive>(properties);

    // The test, the Context and, for the mesh, the primitive hold each object.
    auto const counts = std::array{
        mesh->getRefCount(), bsdf->getRefCount(), edf->getRefCount(), primitive->getRefCount()
    };
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    handler.sync();

    // The runtime holds no context object after sync.
    EXPECT_EQ(mesh->getRefCount(), counts[0]);
    EXPECT_EQ(bsdf->getRefCount(), counts[1]);
    EXPECT_EQ(edf->getRefCount(), counts[2]);
    EXPECT_EQ(primitive->getRefCount(), counts[3]);
}
