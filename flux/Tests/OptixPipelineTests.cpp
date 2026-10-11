#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

#include "SceneUtils.h"
#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/EnvMapLight.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
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

/// Renders \p samples samples into a new one-pixel color buffer and returns the pixel.
[[nodiscard]] flux::Spectrum
renderPixel(flux::OptixHandler &handler, flux::Camera const &camera, std::uint32_t samples) {
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{1, 1}, flux::FilmChannels::Color);
    flux::Film film{flux::Vec2u{1, 1}, flux::FilmChannels::Color};
    handler.render(camera, buffer, samples);
    buffer.download(film);
    return film.getChannel<flux::ColorChannel>()[0];
}

/// Downloads the color channel of \p buffer.
[[nodiscard]] std::vector<flux::Spectrum> downloadColor(flux::OptixAccumulationBuffer &buffer) {
    flux::Film film{buffer.getResolution(), buffer.getChannels()};
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

TEST(OptixPipelineTests, RendersNothingForZeroSamples) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{1, 1}, flux::FilmChannels::All);

    EXPECT_EQ(handler.getContext(), context);
    EXPECT_EQ(handler.render(*camera, buffer, 0).paths, 0);
    EXPECT_EQ(buffer.getSampleCount(), 0);
    EXPECT_NO_THROW(handler.render(*camera, buffer, 4));
    EXPECT_EQ(handler.render(*camera, buffer, 0).paths, 0);
    EXPECT_EQ(buffer.getSampleCount(), 4);
}

TEST(OptixPipelineTests, ReportsTheShapeOfABuffer) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    auto const channels = flux::FilmChannels::Normal | flux::FilmChannels::Color;
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{2, 3}, channels);
    EXPECT_EQ(buffer.getResolution(), (flux::Vec2u{2, 3}));
    EXPECT_EQ(buffer.getChannels(), channels);
    EXPECT_EQ(buffer.getSampleCount(), 0);

    // A buffer without channels still counts samples.
    flux::OptixAccumulationBuffer empty(handler, flux::Vec2u{2, 3}, flux::FilmChannels::None);
    EXPECT_EQ(empty.getChannels(), flux::FilmChannels::None);
    EXPECT_NO_THROW(handler.render(*camera, empty, 2));
    EXPECT_EQ(empty.getSampleCount(), 2);
}

TEST(OptixPipelineTests, RejectsARenderBeyondThePathIndex) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    // 65535 pixels at 65538 samples are more paths than a 32-bit path index counts. The buffer
    // holds no channels, so it holds no storage.
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{255, 257}, flux::FilmChannels::None);
    EXPECT_THROW(handler.render(*flux::Camera::create(), buffer, 65538), std::length_error);
    EXPECT_EQ(buffer.getSampleCount(), 0);
}

TEST(OptixPipelineTests, StartsTheSampleSequenceAtTheSamplerSampleOffset) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

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
        flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
        flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{8, 8}, flux::FilmChannels::Color);
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
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{8, 8}, flux::FilmChannels::Color);
    handler.render(*camera, buffer, 1);
    sampler->setSampleOffset(1);
    handler.sync();
    buffer.clear();
    handler.render(*camera, buffer, 1);
    expectNear(second, downloadColor(buffer));
}

TEST(OptixPipelineTests, ContinuesAnAccumulationAcrossRenders) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto scene = makeNoisyScene();
    flux::OptixHandler handler(scene.context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    constexpr auto shape = flux::Vec2u{8, 8};
    flux::OptixAccumulationBuffer whole(handler, shape, flux::FilmChannels::Color);
    flux::OptixAccumulationBuffer split(handler, shape, flux::FilmChannels::Color);

    handler.render(*scene.camera, whole, 4);
    handler.render(*scene.camera, split, 1);
    auto const firstSample = downloadColor(split);
    handler.render(*scene.camera, split, 3);

    EXPECT_EQ(whole.getSampleCount(), 4);
    EXPECT_EQ(split.getSampleCount(), 4);
    auto const expected = downloadColor(whole);
    EXPECT_NE(firstSample, expected);
    expectNear(expected, downloadColor(split));
}

TEST(OptixPipelineTests, RestartsAnAccumulationAfterClear) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto scene = makeNoisyScene();
    flux::OptixHandler handler(scene.context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{8, 8}, flux::FilmChannels::Color);

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

TEST(OptixPipelineTests, RemovesStalePixelsAfterClear) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{1, 1}, flux::FilmChannels::Color);
    flux::Film film{flux::Vec2u{1, 1}, flux::FilmChannels::Color};

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

TEST(OptixPipelineTests, LeavesTheSampleCountAloneWhenTheSceneChanges) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{1, 1}, flux::FilmChannels::Color);

    handler.render(*camera, buffer, 1);

    (void)context->create<flux::PointLight>(kira::Properties{});
    handler.sync();
    camera->setPosition({0.0F, 0.0F, 1.0F});
    context->setActiveSampler(context->create<flux::IndependentSampler>(kira::Properties{}));
    handler.sync();

    handler.render(*camera, buffer, 2);
    EXPECT_EQ(buffer.getSampleCount(), 3);
}

TEST(OptixPipelineTests, RendersTheSceneAsOfTheLastSync) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

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

TEST(OptixPipelineTests, RejectsABufferFromAnotherHandler) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler owner(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixHandler other(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(owner, flux::Vec2u{1, 1}, flux::FilmChannels::Color);

    EXPECT_THROW(other.render(*camera, buffer, 1), std::invalid_argument);
    EXPECT_EQ(buffer.getSampleCount(), 0);
    EXPECT_NO_THROW(owner.render(*camera, buffer, 1));
    EXPECT_EQ(buffer.getSampleCount(), 1);

    flux::OptixAccumulationBuffer otherBuffer(other, flux::Vec2u{1, 1}, flux::FilmChannels::Color);
    EXPECT_NO_THROW(other.render(*camera, otherBuffer, 1));
}

TEST(OptixPipelineTests, RejectsADownloadIntoAFilmOfAnotherShape) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    auto const colorOf = [](flux::Film const &film) {
        auto const color = film.getChannel<flux::ColorChannel>();
        return std::vector<flux::Spectrum>(color.begin(), color.end());
    };

    // Fill a one-pixel film with the first environment.
    kira::Properties properties;
    properties.set("scale", flux::Spectrum{0.25F, 0.5F, 0.75F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(properties));
    handler.sync();
    flux::OptixAccumulationBuffer small(handler, flux::Vec2u{1, 1}, flux::FilmChannels::Color);
    flux::Film film{flux::Vec2u{1, 1}, flux::FilmChannels::Color};
    handler.render(*camera, small, 1);
    small.download(film);
    auto const expected = std::vector{flux::Spectrum{0.25F, 0.5F, 0.75F}};
    ASSERT_EQ(colorOf(film), expected);

    // A buffer of another resolution and a buffer of other channels hold the second environment.
    properties.set("scale", flux::Spectrum{0.5F, 0.25F, 0.125F});
    context->setActiveEnvMap(context->create<flux::EnvMapLight>(properties));
    handler.sync();
    flux::OptixAccumulationBuffer wide(handler, flux::Vec2u{2, 1}, flux::FilmChannels::Color);
    handler.render(*camera, wide, 1);
    EXPECT_THROW(wide.download(film), std::invalid_argument);
    EXPECT_EQ(colorOf(film), expected);

    flux::OptixAccumulationBuffer more(handler, flux::Vec2u{1, 1}, flux::FilmChannels::All);
    handler.render(*camera, more, 1);
    EXPECT_THROW(more.download(film), std::invalid_argument);
    EXPECT_EQ(film.getChannels(), flux::FilmChannels::Color);
    EXPECT_EQ(colorOf(film), expected);
}

TEST(OptixPipelineTests, RendersTheSameSamplesForAnyChunkSize) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    // The scene fills every channel, and some pixels miss.
    auto scene = makeNoisyScene();
    auto &context = *scene.context;

    struct Image {
        std::vector<flux::Vec3f> color;
        std::vector<flux::Vec3f> normal;
        std::vector<flux::Vec3f> albedo;
    };
    flux::OptixHandler handler(scene.context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    // A launch of one thread, which claims every chunk.
    handler.setLaunchWaves(1e-9);
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{8, 8}, flux::FilmChannels::All);
    flux::Film film{flux::Vec2u{8, 8}, flux::FilmChannels::All};
    auto const render = [&](std::uint32_t chunkSize) {
        kira::Properties props;
        props.set("chunk_size", chunkSize);
        context.setActiveIntegrator(context.create<flux::PathIntegrator>(props));
        handler.sync();
        buffer.clear();
        handler.render(*scene.camera, buffer, 8);
        buffer.download(film);
        auto const copy = [](auto span) {
            return std::vector<flux::Vec3f>(span.begin(), span.end());
        };
        return Image{
            .color = copy(film.getChannel<flux::ColorChannel>()),
            .normal = copy(film.getChannel<flux::NormalChannel>()),
            .albedo = copy(film.getChannel<flux::AlbedoChannel>()),
        };
    };

    auto const expected = render(1);
    // 3 does not divide the 8 samples per pixel. 0 and 64 lie outside them.
    for (auto const chunkSize : {0U, 3U, 8U, 64U}) {
        SCOPED_TRACE(chunkSize);
        auto const actual = render(chunkSize);
        expectNear(expected.color, actual.color);
        expectNear(expected.normal, actual.normal);
        expectNear(expected.albedo, actual.albedo);
    }
}

TEST(OptixPipelineTests, RejectsANullContext) {
    EXPECT_THROW(
        (void)flux::OptixHandler(flux::Ref<flux::Context>{}, std::filesystem::path{}),
        std::invalid_argument
    );
}

TEST(OptixPipelineTests, ReleasesContextAfterConstructionFails) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    EXPECT_THROW((void)flux::OptixHandler(context, std::filesystem::path{}), kira::Anyhow);
    EXPECT_EQ(context.getRefCount(), 1);

    auto camera = flux::Camera::create();
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    flux::OptixAccumulationBuffer buffer(handler, flux::Vec2u{1, 1}, flux::FilmChannels::All);
    EXPECT_NO_THROW(handler.render(*camera, buffer, 1));
}

TEST(OptixPipelineTests, RendersImageEnvironmentMapOnMiss) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

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
    auto camera = flux::Camera::create(cameraProps);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    auto const color = renderPixel(handler, *camera, 1);
    EXPECT_NEAR(color.x(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.y(), 0.5F, 1.0e-5F);
    EXPECT_NEAR(color.z(), 0.5F, 1.0e-5F);
}

TEST(OptixPipelineTests, HoldsNoContextObjectAfterSync) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
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

TEST(OptixPipelineTests, SyncFailsWithoutAnActiveIntegratorOrSampler) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    flux::test::setActiveDefaults(*context);
    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    auto const integratorId = context->getActiveIntegrator()->getContextId();
    auto const samplerId = context->getActiveSampler()->getContextId();

    context->remove(integratorId);
    EXPECT_THROW(handler.sync(), kira::Anyhow);
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(kira::Properties{}));
    EXPECT_NO_THROW(handler.sync());

    context->remove(samplerId);
    EXPECT_THROW(handler.sync(), kira::Anyhow);
}
