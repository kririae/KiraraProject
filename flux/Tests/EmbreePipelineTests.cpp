#include <OpenImageIO/imageio.h> // NOLINT(llvm-include-order)
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "SceneUtils.h"
#include "TestUtils.h"
#include "flux/Embree/EmbreeAccumulationBuffer.h"
#include "flux/Embree/EmbreeHandler.h"
#include "flux/IO/ImageIO.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Film.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
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

/// Renders \p samples samples into a new one-pixel color buffer and returns the pixel.
[[nodiscard]] flux::Spectrum
renderPixel(flux::EmbreeHandler &handler, flux::Camera const &camera, std::uint32_t samples) {
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}
    );
    flux::Film film{flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}};
    handler.render(camera, buffer, samples);
    buffer.download(film);
    return film.getChannel<flux::ColorChannel>()[0];
}

/// Downloads the color channel of \p buffer.
[[nodiscard]] std::vector<flux::Spectrum> downloadColor(flux::EmbreeAccumulationBuffer &buffer) {
    flux::Film film{buffer.getSpec()};
    buffer.download(film);
    auto const color = film.getChannel<flux::ColorChannel>();
    return {color.begin(), color.end()};
}

template <typename Value>
void expectNear(std::vector<Value> const &expected, std::vector<Value> const &actual) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t pixel = 0; pixel < expected.size(); ++pixel)
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(actual[pixel][axis], expected[pixel][axis], 1e-5F) << pixel;
}

/// A diffuse triangle under a constant environment.
/// The camera sees the triangle and the sky.
/// The samples of a pixel differ.
struct NoisyScene {
    flux::Ref<flux::Context> context;
    flux::Ref<flux::Camera> camera;
};

[[nodiscard]] NoisyScene makeNoisyScene() {
    NoisyScene scene{.context = flux::Context::create()};
    auto &context = *scene.context;
    flux::test::setActiveDefaults(context);
    context.setActiveEnvMap(context.create<flux::EnvMapLight>(kira::Properties{}));
    auto mesh = context.create<flux::TriangleMesh>(triangleData());
    auto bsdf = context.create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context.create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    cameraProperties.set("fov", 60.0F);
    scene.camera = flux::Camera::create(cameraProperties);
    return scene;
}
} // namespace

TEST(EmbreePipelineTests, StartsTheSampleSequenceAtTheSamplerSampleOffset) {
    // A diffuse triangle under a constant environment, so a sample's value depends on its index.
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto sampler = context->create<flux::IndependentSampler>(kira::Properties{});
    context->setActiveSampler(sampler);
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(kira::Properties{}));
    auto mesh = context->create<flux::TriangleMesh>(triangleData());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    auto camera = flux::Camera::create(cameraProperties);

    // Renders with a fresh handler, so the offset reaches it through the sampler alone.
    auto const render = [&](std::uint64_t offset, std::uint32_t samples) {
        sampler->setSampleOffset(offset);
        flux::EmbreeHandler handler(context);
        flux::EmbreeAccumulationBuffer buffer(
            flux::FilmSpec{flux::Vec2u{8, 8}, flux::FilmChannels::Color}
        );
        handler.render(*camera, buffer, samples);
        return downloadColor(buffer);
    };

    auto const first = render(0, 1);
    auto const second = render(1, 1);
    auto const both = render(0, 2);
    ASSERT_EQ(second.size(), first.size());
    ASSERT_EQ(both.size(), first.size());

    float difference = 0.0F;
    for (std::size_t pixel = 0; pixel < first.size(); ++pixel) {
        for (int axis = 0; axis < 3; ++axis) {
            difference = std::max(difference, std::abs(second[pixel][axis] - first[pixel][axis]));
            EXPECT_NEAR(both[pixel][axis], 0.5F * (first[pixel][axis] + second[pixel][axis]), 1e-5F)
                << pixel;
        }
    }
    EXPECT_GT(difference, 1e-3F);

    // An offset edit reaches a handler that already exists through sync.
    sampler->setSampleOffset(0);
    flux::EmbreeHandler handler(context);
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{8, 8}, flux::FilmChannels::Color}
    );
    handler.render(*camera, buffer, 1);
    sampler->setSampleOffset(1);
    handler.sync();
    buffer.clear();
    handler.render(*camera, buffer, 1);
    expectNear(second, downloadColor(buffer));
}

TEST(EmbreePipelineTests, RendersSecondInstanceAndDownloadsFilmChannels) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);

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

    flux::EmbreeHandler handler(context);
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::All}
    );
    flux::Film film{flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::All}};
    auto const stats = handler.render(*camera, buffer, 2);
    buffer.download(film);

    EXPECT_EQ(stats.paths, 2);
    EXPECT_EQ(buffer.getSampleCount(), 2);
    auto const normal = film.getChannel<flux::NormalChannel>();
    ASSERT_EQ(normal.size(), 1);
    EXPECT_NEAR(normal[0].x(), -inverseSqrtTwo, 1.0e-5F);
    EXPECT_NEAR(normal[0].y(), 0.0F, 1.0e-5F);
    EXPECT_NEAR(normal[0].z(), inverseSqrtTwo, 1.0e-5F);
    auto const albedo = film.getChannel<flux::AlbedoChannel>();
    ASSERT_EQ(albedo.size(), 1);
    EXPECT_NEAR(albedo[0].x(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(albedo[0].y(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(albedo[0].z(), 0.5F, 1.0e-5F);

    auto const outputRoot = std::filesystem::path(FLUX_TEST_OUTPUT_DIR);
    auto const outputPath = outputRoot / "nested" / "normal.exr";
    std::filesystem::remove_all(outputRoot);
    flux::writeImage<flux::NormalChannel>(outputPath, film);

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

    flux::Film const missing{flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Normal}};
    EXPECT_THROW(
        flux::writeImage<flux::AlbedoChannel>(outputRoot / "missing.exr", missing), kira::Anyhow
    );
}

TEST(EmbreePipelineTests, RejectsANullContext) {
    EXPECT_THROW((void)flux::EmbreeHandler(flux::Ref<flux::Context>{}), std::invalid_argument);
}

TEST(EmbreePipelineTests, RejectsSingularInstanceTransforms) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);

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
    flux::test::setActiveDefaults(*context);
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

TEST(EmbreePipelineTests, RendersWithIndicesThatFollowUnusedObjectsAndAfterAResync) {
    // Light a triangle with an emitter and return the center pixel.
    auto const render = [&](bool withUnusedMesh, bool withHiddenPrimitive, bool withResync) {
        auto context = flux::Context::create();
        flux::test::setActiveDefaults(*context);

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

        flux::EmbreeHandler handler(context);
        auto const color = [&] { return renderPixel(handler, *camera, 4); };
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

TEST(EmbreePipelineTests, AddsNoRadianceAtZeroMaxDepth) {
    // Render one sample of a pixel that sees either the environment or an emitter.
    auto const render = [](std::uint32_t maxDepth, bool seesEmitter) {
        auto context = flux::Context::create();
        flux::test::setActiveDefaults(*context);
        kira::Properties integratorProperties;
        integratorProperties.set("max_depth", maxDepth);
        context->setActiveIntegrator(context->create<flux::PathIntegrator>(integratorProperties));
        context->setActiveEnvMap(context->create<flux::EnvMapLight>(kira::Properties{}));

        if (seesEmitter) {
            auto mesh = context->create<flux::TriangleMesh>(triangleData());
            auto edf = context->create<flux::ConstantEDF>(kira::Properties{});
            kira::Properties emitterProperties;
            emitterProperties.set(
                "geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId())
            );
            emitterProperties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
            (void)context->create<flux::Primitive>(emitterProperties);
        }

        kira::Properties cameraProperties;
        cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
        cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
        cameraProperties.set("fov", 1.0F);
        auto camera = flux::Camera::create(cameraProperties);

        flux::EmbreeHandler handler(context);
        return renderPixel(handler, *camera, 1);
    };

    EXPECT_GT(render(1, false).x(), 0.0F);
    EXPECT_GT(render(1, true).x(), 0.0F);
    EXPECT_EQ(render(0, false), flux::Spectrum{});
    EXPECT_EQ(render(0, true), flux::Spectrum{});
}

TEST(EmbreePipelineTests, RendersDirectLightIntoColorChannel) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);

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

    flux::EmbreeHandler handler(context);
    auto const color = renderPixel(handler, *camera, 4);
    EXPECT_GT(color.x(), 0.0F);
    EXPECT_GT(color.y(), 0.0F);
    EXPECT_GT(color.z(), 0.0F);

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
    EXPECT_EQ(renderPixel(handler, *camera, 4), flux::Spectrum{});
}

TEST(EmbreePipelineTests, RendersImageEnvironmentMapOnMiss) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    kira::Properties textureProps;
    textureProps.set("type", "image");
    textureProps.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Texture2x2.ppm");
    kira::Properties lightProps;
    lightProps.set("texture", textureProps);
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(lightProps));

    kira::Properties cameraProps;
    cameraProps.set("fov", 1.0e-4F);
    flux::EmbreeHandler handler(context);
    auto const color = renderPixel(handler, *flux::Camera::create(cameraProps), 1);
    EXPECT_NEAR(color.x(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.y(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.z(), 0.5F, 1.0e-5F);
}

TEST(EmbreePipelineTests, ReportsTheShapeOfABuffer) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::EmbreeHandler handler(context);

    auto const channels = flux::FilmChannels::Normal | flux::FilmChannels::Color;
    flux::EmbreeAccumulationBuffer buffer(flux::FilmSpec{flux::Vec2u{2, 3}, channels});
    EXPECT_EQ(buffer.getSpec(), (flux::FilmSpec{flux::Vec2u{2, 3}, channels}));
    EXPECT_EQ(buffer.getSampleCount(), 0);

    // A buffer without channels still counts samples.
    flux::EmbreeAccumulationBuffer empty(
        flux::FilmSpec{flux::Vec2u{2, 3}, flux::FilmChannels::None}
    );
    EXPECT_EQ(empty.getSpec().channels, flux::FilmChannels::None);
    EXPECT_NO_THROW(handler.render(*camera, empty, 2));
    EXPECT_EQ(empty.getSampleCount(), 2);
}

TEST(EmbreePipelineTests, ContinuesAnAccumulationAcrossRenders) {
    auto scene = makeNoisyScene();
    flux::EmbreeHandler handler(scene.context);
    constexpr auto spec = flux::FilmSpec{flux::Vec2u{8, 8}, flux::FilmChannels::Color};
    flux::EmbreeAccumulationBuffer whole(spec);
    flux::EmbreeAccumulationBuffer split(spec);

    handler.render(*scene.camera, whole, 4);
    handler.render(*scene.camera, split, 1);
    auto const firstSample = downloadColor(split);
    EXPECT_EQ(handler.render(*scene.camera, split, 0).paths, 0);
    EXPECT_EQ(split.getSampleCount(), 1);
    handler.render(*scene.camera, split, 3);

    EXPECT_EQ(whole.getSampleCount(), 4);
    EXPECT_EQ(split.getSampleCount(), 4);
    auto const expected = downloadColor(whole);
    EXPECT_NE(firstSample, expected);
    expectNear(expected, downloadColor(split));
}

TEST(EmbreePipelineTests, RestartsAnAccumulationAfterClear) {
    auto scene = makeNoisyScene();
    flux::EmbreeHandler handler(scene.context);
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{8, 8}, flux::FilmChannels::Color}
    );

    handler.render(*scene.camera, buffer, 2);
    auto const expected = downloadColor(buffer);
    handler.render(*scene.camera, buffer, 3);
    EXPECT_EQ(buffer.getSampleCount(), 5);

    buffer.clear();
    EXPECT_EQ(buffer.getSampleCount(), 0);
    handler.render(*scene.camera, buffer, 2);
    EXPECT_EQ(buffer.getSampleCount(), 2);
    expectNear(expected, downloadColor(buffer));
}

TEST(EmbreePipelineTests, RemovesStalePixelsAfterClear) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::EmbreeHandler handler(context);
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}
    );
    flux::Film film{flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}};

    // A scale of NaN fills the pixel with NaN.
    kira::Properties nanProperties;
    nanProperties.set("scale", flux::Spectrum{std::numeric_limits<float>::quiet_NaN()});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(nanProperties));
    handler.sync();
    handler.render(*camera, buffer, 1);
    buffer.download(film);
    ASSERT_TRUE(std::isnan(film.getChannel<flux::ColorChannel>()[0].x()));

    kira::Properties lightProperties;
    lightProperties.set("scale", flux::Spectrum{0.25F, 0.5F, 0.75F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(lightProperties));
    handler.sync();
    buffer.clear();
    handler.render(*camera, buffer, 1);
    buffer.download(film);
    EXPECT_EQ(film.getChannel<flux::ColorChannel>()[0], (flux::Spectrum{0.25F, 0.5F, 0.75F}));
}

TEST(EmbreePipelineTests, LeavesTheSampleCountAloneWhenTheSceneChanges) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::EmbreeHandler handler(context);
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}
    );

    handler.render(*camera, buffer, 1);

    (void)context->create<flux::PointLight>(kira::Properties{});
    handler.sync();
    camera->setPosition({0.0F, 0.0F, 1.0F});
    context->setActiveSampler(context->create<flux::IndependentSampler>(kira::Properties{}));
    handler.sync();

    handler.render(*camera, buffer, 2);
    EXPECT_EQ(buffer.getSampleCount(), 3);
}

TEST(EmbreePipelineTests, RendersTheSceneAsOfTheLastSync) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::EmbreeHandler handler(context);

    kira::Properties lightProperties;
    lightProperties.set("scale", flux::Spectrum{0.25F, 0.5F, 0.75F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(lightProperties));
    handler.sync();
    EXPECT_EQ(renderPixel(handler, *camera, 1), (flux::Spectrum{0.25F, 0.5F, 0.75F}));

    lightProperties.set("scale", flux::Spectrum{0.5F, 0.25F, 0.125F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(lightProperties));
    EXPECT_EQ(renderPixel(handler, *camera, 1), (flux::Spectrum{0.25F, 0.5F, 0.75F}));

    handler.sync();
    EXPECT_EQ(renderPixel(handler, *camera, 1), (flux::Spectrum{0.5F, 0.25F, 0.125F}));
}

TEST(EmbreePipelineTests, RejectsADownloadIntoAFilmOfAnotherShape) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::EmbreeHandler handler(context);
    auto const colorOf = [](flux::Film const &film) {
        auto const color = film.getChannel<flux::ColorChannel>();
        return std::vector<flux::Spectrum>(color.begin(), color.end());
    };

    // Fill a one-pixel film with the first environment.
    kira::Properties properties;
    properties.set("scale", flux::Spectrum{0.25F, 0.5F, 0.75F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(properties));
    handler.sync();
    flux::EmbreeAccumulationBuffer small(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}
    );
    flux::Film film{flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}};
    handler.render(*camera, small, 1);
    small.download(film);
    auto const expected = std::vector{flux::Spectrum{0.25F, 0.5F, 0.75F}};
    ASSERT_EQ(colorOf(film), expected);

    // A buffer of another resolution and a buffer of other channels hold the second environment.
    properties.set("scale", flux::Spectrum{0.5F, 0.25F, 0.125F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(properties));
    handler.sync();
    flux::EmbreeAccumulationBuffer wide(
        flux::FilmSpec{flux::Vec2u{2, 1}, flux::FilmChannels::Color}
    );
    handler.render(*camera, wide, 1);
    EXPECT_THROW(wide.download(film), std::invalid_argument);
    EXPECT_EQ(colorOf(film), expected);

    flux::EmbreeAccumulationBuffer more(flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::All});
    handler.render(*camera, more, 1);
    EXPECT_THROW(more.download(film), std::invalid_argument);
    EXPECT_EQ(film.getSpec().channels, flux::FilmChannels::Color);
    EXPECT_EQ(colorOf(film), expected);

    // A film that was moved from holds no channels, so the shapes differ.
    flux::Film const saved{std::move(film)};
    EXPECT_THROW(small.download(film), std::invalid_argument); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(colorOf(saved), expected);
}

namespace {
/// A lit triangle and, optionally, a doomed primitive that has its own mesh and BSDF.
struct RemovalScene {
    flux::Ref<flux::Context> context;
    flux::Ref<flux::Camera> camera;
    flux::Ref<flux::Primitive> doomed;

    /// The vertex array of the doomed mesh, held by no one in the scene once the mesh is gone.
    std::weak_ptr<void const> doomedVertices;
};

/// \brief Builds the scene, creating the doomed primitive first so it holds index 0.
///
/// The doomed primitive emits and sits beside the triangle, or does not emit and covers the
/// triangle at the center pixel, so camera rays hit it.
[[nodiscard]] RemovalScene makeRemovalScene(bool withDoomed, bool doomedEmits) {
    RemovalScene scene{.context = flux::Context::create()};
    auto &context = *scene.context;
    flux::test::setActiveDefaults(context);

    if (withDoomed) {
        auto doomedData = triangleData();
        scene.doomedVertices = doomedData.vertices;
        auto doomedMesh = context.create<flux::TriangleMesh>(std::move(doomedData));
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
    scene.camera = flux::Camera::create(cameraProperties);
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
    auto const color = [](flux::EmbreeHandler &handler, flux::Camera const &camera) {
        return renderPixel(handler, camera, 4);
    };
    auto fresh = makeRemovalScene(false, true);
    flux::EmbreeHandler freshHandler(fresh.context);
    auto const expected = color(freshHandler, *fresh.camera);
    EXPECT_GT(expected.x(), 0.0F);

    auto scene = makeRemovalScene(true, true);
    flux::EmbreeHandler handler(scene.context);
    auto const lit = color(handler, *scene.camera);
    removeDoomed(scene);
    handler.sync();

    auto const actual = color(handler, *scene.camera);
    EXPECT_NE(actual, lit);
    EXPECT_EQ(actual, expected);
}

TEST(EmbreePipelineTests, HoldsACollectedMeshUntilTheNextSync) {
    // The doomed primitive is in the view, so its presence changes the image.
    auto fresh = makeRemovalScene(false, false);
    flux::EmbreeHandler freshHandler(fresh.context);
    auto const without = renderPixel(freshHandler, *fresh.camera, 4);

    auto scene = makeRemovalScene(true, false);
    flux::EmbreeHandler handler(scene.context);
    EXPECT_NE(renderPixel(handler, *scene.camera, 4), without);

    // Embree holds the vertex array of the collected mesh until a sync drops it.
    removeDoomed(scene);
    EXPECT_FALSE(scene.doomedVertices.expired());
    EXPECT_NE(renderPixel(handler, *scene.camera, 4), without);

    handler.sync();
    EXPECT_EQ(renderPixel(handler, *scene.camera, 4), without);
}

TEST(EmbreePipelineTests, SyncFailsForGoodWithoutAnActiveIntegratorOrSampler) {
    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::EmbreeHandler handler(context);
    flux::EmbreeAccumulationBuffer buffer(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}
    );
    auto const integratorId = context->getActiveIntegrator()->getContextId();
    auto const samplerId = context->getActiveSampler()->getContextId();

    context->remove(integratorId);
    EXPECT_FALSE(handler.hasFailed());
    EXPECT_THROW(handler.sync(), kira::Anyhow);
    EXPECT_TRUE(handler.hasFailed());

    // Fixing the context does not revive the handler.
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(kira::Properties{}));
    EXPECT_NO_THROW(handler.sync());
    EXPECT_EQ(handler.render(*camera, buffer, 1).paths, 0);
    EXPECT_EQ(buffer.getSampleCount(), 0);
    EXPECT_TRUE(handler.hasFailed());

    // A new handler renders the fixed context, and fails when the sampler is gone.
    flux::EmbreeHandler replacement(context);
    flux::EmbreeAccumulationBuffer replacementBuffer(
        flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Color}
    );
    EXPECT_EQ(replacement.render(*camera, replacementBuffer, 1).paths, 1);
    EXPECT_FALSE(replacement.hasFailed());

    context->remove(samplerId);
    EXPECT_THROW(replacement.sync(), kira::Anyhow);
    EXPECT_TRUE(replacement.hasFailed());
}

namespace {
/// A lit triangle, and optionally an occluder that covers the center pixel.
struct SwapScene {
    flux::Ref<flux::Context> context;
    flux::Ref<flux::Camera> camera;
    flux::Ref<flux::Primitive> old;
    std::size_t oldMeshId{};
    std::size_t oldBSDFId{};
};

/// \brief Creates an occluder with its own mesh and BSDF, and returns its primitive.
///
/// \p size scales the mesh and \p reflectance sets the BSDF, so two occluders render differently.
[[nodiscard]] flux::Ref<flux::Primitive>
addOccluder(flux::Context &context, float size, float reflectance) {
    auto mesh = context.create<flux::TriangleMesh>(flux::TriangleMesh::Data{
        .vertices = flux::test::sharedBuffer(
            flux::Vec3f{-size, -size, 0.0F}, flux::Vec3f{3.0F * size, -size, 0.0F},
            flux::Vec3f{-size, 3.0F * size, 0.0F}
        ),
        .triangles = flux::test::sharedBuffer(flux::Vec3u{0, 1, 2}),
    });
    kira::Properties bsdfProperties;
    bsdfProperties.set("R", flux::Spectrum{reflectance});
    auto bsdf = context.create<flux::DiffuseBSDF>(bsdfProperties);
    auto primitive = context.create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));
    primitive->setTransform(
        {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F}
    );
    return primitive;
}

/// \brief Builds the scene, creating the old occluder first so it holds index 0.
[[nodiscard]] SwapScene makeSwapScene(bool withOld) {
    SwapScene scene{.context = flux::Context::create()};
    auto &context = *scene.context;
    flux::test::setActiveDefaults(context);
    if (withOld) {
        scene.old = addOccluder(context, 2.0F, 0.1F);
        scene.oldMeshId = scene.old->getGeometry()->getContextId();
        scene.oldBSDFId = scene.old->getBSDF()->getContextId();
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
    scene.camera = flux::Camera::create(cameraProperties);
    return scene;
}
} // namespace

TEST(EmbreePipelineTests, RendersAnObjectThatReusesAFreedIndexLikeAFreshContext) {
    auto const color = [](flux::EmbreeHandler &handler, flux::Camera const &camera) {
        return renderPixel(handler, camera, 4);
    };

    // The fresh context never held the old occluder.
    auto fresh = makeSwapScene(false);
    auto const freshOccluder = addOccluder(*fresh.context, 1.0F, 0.9F);
    flux::EmbreeHandler freshHandler(fresh.context);
    auto const expected = color(freshHandler, *fresh.camera);

    auto scene = makeSwapScene(true);
    auto &context = *scene.context;
    flux::EmbreeHandler handler(scene.context);
    auto const before = color(handler, *scene.camera);
    auto const primitiveIndex = context.getIndex<flux::Primitive>(scene.old->getContextId());
    auto const meshIndex = context.getIndex<flux::Geometry>(scene.oldMeshId);
    auto const bsdfIndex = context.getIndex<flux::BSDF>(scene.oldBSDFId);

    // Remove the old occluder and its dependents, then free their indices.
    context.remove(scene.old->getContextId());
    scene.old.reset();
    context.collectGarbage();
    context.clearDirty();
    EXPECT_FALSE(context.findIndex<flux::Geometry>(scene.oldMeshId));

    // A new occluder with another mesh and BSDF takes the freed indices.
    auto const replacement = addOccluder(context, 1.0F, 0.9F);
    EXPECT_EQ(context.getIndex<flux::Primitive>(replacement->getContextId()), primitiveIndex);
    EXPECT_EQ(
        context.getIndex<flux::Geometry>(replacement->getGeometry()->getContextId()), meshIndex
    );
    EXPECT_EQ(context.getIndex<flux::BSDF>(replacement->getBSDF()->getContextId()), bsdfIndex);

    handler.sync();
    auto const actual = color(handler, *scene.camera);
    EXPECT_NE(actual, before);
    EXPECT_EQ(actual, expected);
}
