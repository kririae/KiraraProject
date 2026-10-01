#include <OpenImageIO/imageio.h> // NOLINT(llvm-include-order)
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "TestUtils.h"
#include "flux/Embree/EmbreeHandler.h"
#include "flux/IO/ImageIO.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

#ifndef FLUX_TEST_OUTPUT_DIR
#error "FLUX_TEST_OUTPUT_DIR must name the Flux test output directory"
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

[[nodiscard]] kira::Properties renderProductProperties(
    std::uint32_t width = 1, std::uint32_t height = 1, std::uint32_t samplesPerPixel = 1
) {
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{width, height});
    properties.set("num_samples", samplesPerPixel);
    return properties;
}
} // namespace

TEST(EmbreePipelineTests, RendersSecondInstanceAndDownloadsFilmChannels) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(meshProperties);
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    auto primitive = context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    primitive->setTransform({
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        1.0F,
        0.0F,
    });

    constexpr auto inverseSqrtTwo = 0.70710678F;
    kira::Properties cameraProperties;
    cameraProperties.set(
        "position", flux::Vec3f{0.25F - inverseSqrtTwo, 0.25F, 0.25F + inverseSqrtTwo}
    );
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.25F});
    cameraProperties.set("fov", 1.0F);
    auto camera = flux::Camera::create(cameraProperties);
    auto product = flux::RenderProduct::create(camera, renderProductProperties(1, 1, 2));

    flux::EmbreeHandler handler(context);
    auto const stats = handler.render(*product, 2);
    handler.download(*product);

    EXPECT_EQ(stats.paths, 2);
    auto const normal = product->getFilm().getChannel<flux::NormalChannel>();
    ASSERT_EQ(normal.size(), 1);
    EXPECT_NEAR(normal[0].x(), -inverseSqrtTwo, 1.0e-5F);
    EXPECT_NEAR(normal[0].y(), 0.0F, 1.0e-5F);
    EXPECT_NEAR(normal[0].z(), inverseSqrtTwo, 1.0e-5F);
    auto const albedo = product->getFilm().getChannel<flux::AlbedoChannel>();
    ASSERT_EQ(albedo.size(), 1);
    EXPECT_NEAR(albedo[0].x(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(albedo[0].y(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(albedo[0].z(), 0.5F, 1.0e-5F);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);
    EXPECT_TRUE(handler.isConverged(*product));

    auto const outputRoot = std::filesystem::path(FLUX_TEST_OUTPUT_DIR);
    auto const outputPath = outputRoot / "nested" / "normal.exr";
    std::filesystem::remove_all(outputRoot);
    flux::writeImage<flux::NormalChannel>(outputPath, product->getFilm());

    auto input = OIIO::ImageInput::open(outputPath.string());
    ASSERT_TRUE(input) << OIIO::geterror();
    auto const &specification = input->spec();
    EXPECT_EQ(specification.width, 1);
    EXPECT_EQ(specification.height, 1);
    EXPECT_EQ(specification.nchannels, 3);

    std::array<float, 3> pixels{};
    ASSERT_TRUE(input->read_image(0, 0, 0, 3, OIIO::span<float>{pixels.data(), pixels.size()}))
        << input->geterror();
    EXPECT_NEAR(pixels[0], normal[0].x(), 1.0e-6F);
    EXPECT_NEAR(pixels[1], normal[0].y(), 1.0e-6F);
    EXPECT_NEAR(pixels[2], normal[0].z(), 1.0e-6F);
    EXPECT_TRUE(input->close());
    std::filesystem::remove_all(outputRoot);

    flux::Film savedFilm(1, 1);
    swap(savedFilm, product->getFilm());
    EXPECT_EQ(savedFilm.getChannel<flux::NormalChannel>().size(), 1);
    EXPECT_TRUE(product->getFilm().getChannel<flux::NormalChannel>().empty());
    swap(savedFilm, product->getFilm());
}

TEST(EmbreePipelineTests, RejectsSingularInstanceTransforms) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(meshProperties);
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto primitive = context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    primitive->setTransform({
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
    });

    EXPECT_THROW((void)flux::EmbreeHandler(context), kira::Anyhow);
}

TEST(EmbreePipelineTests, RejectsAVertexBufferWithoutSpareCapacity) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    auto const corners = std::array{
        flux::Vec3f{0.0F, 0.0F, 0.0F},
        flux::Vec3f{1.0F, 0.0F, 0.0F},
        flux::Vec3f{0.0F, 1.0F, 0.0F},
    };
    auto vertices = std::make_shared<flux::HostBuffer<flux::Vec3f>>();
    vertices->resize(corners.size());
    std::ranges::copy(corners, vertices->data());
    auto mesh = context->create<flux::TriangleMesh>(flux::TriangleMesh::Data{
        .vertices = std::move(vertices),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
    });
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));

    try {
        (void)flux::EmbreeHandler(context);
        ADD_FAILURE() << "Embree accepted a vertex buffer without spare capacity";
    } catch (kira::Anyhow const &error) {
        EXPECT_NE(std::string(error.what()).find("spare capacity"), std::string::npos)
            << error.what();
    }
}

TEST(EmbreePipelineTests, HoldsTheMeshArraysItReads) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    auto vertices = flux::test::sharedBuffer(
        flux::Vec3f{0.0F, 0.0F, 0.0F}, flux::Vec3f{1.0F, 0.0F, 0.0F}, flux::Vec3f{0.0F, 1.0F, 0.0F}
    );
    auto mesh = context->create<flux::TriangleMesh>(flux::TriangleMesh::Data{
        .vertices = vertices,
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
        .texCoords = flux::test::sharedBuffer(
            flux::Vec2f{0.0F, 0.0F}, flux::Vec2f{1.0F, 0.0F}, flux::Vec2f{0.0F, 1.0F}
        ),
    });
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));

    // Count the holders of each array the Impl reads, generated normals included.
    auto const &data = mesh->getData();
    auto const counts = std::array{
        data.vertices.use_count(), data.triangles.use_count(), data.normals.use_count(),
        data.texCoords.use_count()
    };
    auto handler = flux::EmbreeHandler(context);

    // Embree holds each array. use_count is the only observable, since the
    // entries are private.
    EXPECT_GT(data.vertices.use_count(), counts[0]);
    EXPECT_GT(data.triangles.use_count(), counts[1]);
    EXPECT_GT(data.normals.use_count(), counts[2]);
    EXPECT_GT(data.texCoords.use_count(), counts[3]);
}

TEST(EmbreePipelineTests, RendersWithIndicesThatFollowUnusedObjectsAndAfterAResync) {
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

        flux::EmbreeHandler handler(context);
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

TEST(EmbreePipelineTests, RendersDirectLightIntoColorChannel) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(meshProperties);
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));

    kira::Properties lightProperties;
    lightProperties.set("position", flux::Vec3f{0.75F, 0.25F, 1.0F});
    lightProperties.set("intensity", flux::Spectrum{1.0F, 1.0F, 1.0F});
    (void)context->create<flux::PointLight>(lightProperties);

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    cameraProperties.set("fov", 1.0F);
    auto camera = flux::Camera::create(cameraProperties);
    auto product = flux::RenderProduct::create(camera, renderProductProperties(1, 1, 4));
    product->getFilm().setChannels(flux::FilmChannels::Color);

    flux::EmbreeHandler handler(context);
    handler.render(*product, 4);
    handler.download(*product);

    auto const color = product->getFilm().getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 1);
    EXPECT_GT(color[0].x(), 0.0F);
    EXPECT_GT(color[0].y(), 0.0F);
    EXPECT_GT(color[0].z(), 0.0F);

    kira::Properties blockerProperties;
    blockerProperties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    auto blocker = context->create<flux::Primitive>(blockerProperties);
    blocker->setTransform({
        0.0F,
        0.0F,
        1.0F,
        0.5F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
    });

    handler.sync();
    handler.render(*product, 4);
    handler.download(*product);
    auto const blockedColor = product->getFilm().getChannel<flux::ColorChannel>();
    EXPECT_EQ(blockedColor[0], flux::Spectrum{});
}

TEST(EmbreePipelineTests, RendersConstantEnvironmentMapOnMiss) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    kira::Properties lightProps;
    lightProps.set("scale", flux::Spectrum{0.25F, 0.5F, 0.75F});
    (void)context->create<flux::EnvMapLight>(lightProps);

    auto product =
        flux::RenderProduct::create(flux::Camera::create(), renderProductProperties(1, 1, 1));
    product->getFilm().setChannels(flux::FilmChannels::Color);
    flux::EmbreeHandler handler(context);
    handler.render(*product, 1);
    handler.download(*product);

    auto const color = product->getFilm().getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 1);
    EXPECT_EQ(color.front(), (flux::Spectrum{0.25F, 0.5F, 0.75F}));
}

TEST(EmbreePipelineTests, RendersImageEnvironmentMapOnMiss) {
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
    auto product = flux::RenderProduct::create(
        flux::Camera::create(cameraProps), renderProductProperties(1, 1, 1)
    );
    product->getFilm().setChannels(flux::FilmChannels::Color);
    flux::EmbreeHandler handler(context);
    handler.render(*product, 1);
    handler.download(*product);

    auto const color = product->getFilm().getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 1);
    EXPECT_NEAR(color.front().x(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.front().y(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.front().z(), 0.5F, 1.0e-5F);
}

TEST(EmbreePipelineTests, InvalidatesAccumulationForCameraFilmAndSync) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>(kira::Properties{});
    (void)context->create<flux::IndependentSampler>(kira::Properties{});
    auto camera = flux::Camera::create();
    auto product = flux::RenderProduct::create(camera, renderProductProperties(1, 1, 4));
    product->getFilm().setChannels(flux::FilmChannels::Normal);
    flux::EmbreeHandler handler(context);

    EXPECT_THROW(handler.download(*product), kira::Anyhow);
    EXPECT_THROW(
        flux::writeImage<flux::NormalChannel>(
            std::filesystem::path(FLUX_TEST_OUTPUT_DIR) / "missing.exr", product->getFilm()
        ),
        kira::Anyhow
    );
    EXPECT_THROW(handler.render(*product, 0), std::invalid_argument);
    handler.render(*product, 2);
    handler.download(*product);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);
    EXPECT_FALSE(handler.isConverged(*product));
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 1);

    product->setSamplesPerPixel(2);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 2);
    EXPECT_TRUE(handler.isConverged(*product));

    camera->setPosition({0.0F, 0.0F, 1.0F});
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 1);

    handler.render(*product, 1);
    product->getFilm().setResolution(2, 1);
    EXPECT_TRUE(product->getFilm().getChannel<flux::NormalChannel>().empty());
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    handler.render(*product, 1);
    handler.download(*product);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 2);

    handler.sync();
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 2);

    handler.release(*product);
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 2);

    product->getFilm().setResolution(2, 1);
    product->getFilm().setChannels(flux::FilmChannels::Normal);
    EXPECT_EQ(product->getFilm().getChannel<flux::NormalChannel>().size(), 2);

    product->getFilm().setChannels(flux::FilmChannels::Albedo);
    EXPECT_TRUE(product->getFilm().getChannel<flux::NormalChannel>().empty());
    EXPECT_TRUE(product->getFilm().getChannel<flux::AlbedoChannel>().empty());
}

TEST(EmbreePipelineTests, HoldsNoContextObjectAfterSync) {
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
    flux::EmbreeHandler handler(context);
    handler.sync();

    // The runtime holds no context object after sync.
    EXPECT_EQ(mesh->getRefCount(), counts[0]);
    EXPECT_EQ(bsdf->getRefCount(), counts[1]);
    EXPECT_EQ(edf->getRefCount(), counts[2]);
    EXPECT_EQ(primitive->getRefCount(), counts[3]);
}

namespace {
/// A lit triangle and, optionally, a doomed primitive that has its own mesh and BSDF.
struct RemovalScene {
    flux::Ref<flux::Context> context;
    flux::Ref<flux::RenderProduct> product;
    flux::Ref<flux::Primitive> doomed;
};

/// \brief Builds the scene, creating the doomed primitive first so it holds index 0.
///
/// The doomed primitive emits and sits beside the triangle, or does not emit and covers the
/// triangle at the center pixel, so camera rays hit it.
[[nodiscard]] RemovalScene makeRemovalScene(bool withDoomed, bool doomedEmits) {
    RemovalScene scene{.context = flux::Context::create()};
    auto &context = *scene.context;
    (void)context.create<flux::PathIntegrator>(kira::Properties{});
    (void)context.create<flux::IndependentSampler>(kira::Properties{});

    if (withDoomed) {
        auto doomedMesh = context.create<flux::TriangleMesh>(triangleData());
        auto doomedBSDF = context.create<flux::DiffuseBSDF>(kira::Properties{});
        auto doomedEDF = context.create<flux::ConstantEDF>(kira::Properties{});
        auto properties = primitiveProperties(*doomedMesh, *doomedBSDF);
        if (doomedEmits)
            properties.set("edf_ctx_id", static_cast<std::int64_t>(doomedEDF->getContextId()));
        scene.doomed = context.create<flux::Primitive>(properties);
        if (doomedEmits)
            scene.doomed->setTransform(
                {1.0F, 0.0F, 0.0F, 0.5F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 1.0F}
            );
        else
            scene.doomed->setTransform(
                {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F}
            );
    }

    auto mesh = context.create<flux::TriangleMesh>(triangleData());
    auto bsdf = context.create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context.create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    auto edf = context.create<flux::ConstantEDF>(kira::Properties{});
    auto emitterProperties = primitiveProperties(*mesh, *bsdf);
    emitterProperties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    auto emitter = context.create<flux::Primitive>(emitterProperties);
    emitter->setTransform(
        {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 1.0F}
    );

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    cameraProperties.set("fov", 1.0F);
    kira::Properties productProperties;
    productProperties.set("resolution", flux::Vec2u{1, 1});
    productProperties.set("num_samples", std::uint32_t{4});
    scene.product =
        flux::RenderProduct::create(flux::Camera::create(cameraProperties), productProperties);
    scene.product->getFilm().setChannels(flux::FilmChannels::Color);
    return scene;
}

/// \brief Removes the doomed primitive and collects its dependents.
void removeDoomed(RemovalScene &scene) {
    auto &context = *scene.context;
    auto const geometries = context.getObjects<flux::Geometry>().size();
    context.remove(scene.doomed->getContextId());
    scene.doomed.reset();
    context.collectGarbage();

    // The doomed mesh and BSDF leave with the primitive.
    EXPECT_EQ(context.getObjects<flux::Geometry>().size(), geometries - 1);
}
} // namespace

TEST(EmbreePipelineTests, RendersLikeAContextThatNeverHeldARemovedPrimitive) {
    auto const color = [](flux::EmbreeHandler &handler, flux::RenderProduct &product) {
        handler.render(product, 4);
        handler.download(product);
        return product.getFilm().getChannel<flux::ColorChannel>()[0];
    };
    auto fresh = makeRemovalScene(false, true);
    flux::EmbreeHandler freshHandler(fresh.context);
    auto const expected = color(freshHandler, *fresh.product);
    EXPECT_GT(expected.x(), 0.0F);

    auto scene = makeRemovalScene(true, true);
    flux::EmbreeHandler handler(scene.context);
    auto const lit = color(handler, *scene.product);
    removeDoomed(scene);
    handler.sync();

    auto const actual = color(handler, *scene.product);
    EXPECT_NE(actual, lit);
    EXPECT_EQ(actual, expected);
}

TEST(EmbreePipelineTests, RendersAfterAMeshIsCollectedWithoutSync) {
    auto const color = [](flux::EmbreeHandler &handler, flux::RenderProduct &product) {
        handler.render(product, 4);
        handler.download(product);
        return product.getFilm().getChannel<flux::ColorChannel>()[0];
    };

    // The doomed primitive is in the view, so its presence changes the image.
    auto fresh = makeRemovalScene(false, false);
    flux::EmbreeHandler freshHandler(fresh.context);
    auto const without = color(freshHandler, *fresh.product);
    auto reference = makeRemovalScene(true, false);
    flux::EmbreeHandler referenceHandler(reference.context);
    EXPECT_NE(color(referenceHandler, *reference.product), without);
    auto const expected = color(referenceHandler, *reference.product);

    auto scene = makeRemovalScene(true, false);
    flux::EmbreeHandler handler(scene.context);
    (void)color(handler, *scene.product);

    // Both scenes render twice. Only one removes the primitive and its mesh in between, so
    // traversal reads the vertices after the mesh object is gone.
    removeDoomed(scene);

    EXPECT_EQ(color(handler, *scene.product), expected);
}
