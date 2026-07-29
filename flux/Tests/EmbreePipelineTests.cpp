#include <OpenImageIO/imageio.h> // NOLINT(llvm-include-order)
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "flux/Embree/EmbreeHandler.h"
#include "flux/IO/ImageIO.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
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

[[nodiscard]] kira::Properties renderProductProperties(
    std::uint32_t width = 1, std::uint32_t height = 1, std::uint32_t samplesPerPixel = 1
) {
    kira::Properties properties;
    properties.set("width", width);
    properties.set("height", height);
    properties.set("num_samples", samplesPerPixel);
    return properties;
}
} // namespace

TEST(EmbreePipelineTests, RendersSecondInstanceAndDownloadsFilmChannels) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));
    auto bsdf = context->create<flux::DiffuseBSDF>();
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
    auto camera = flux::Camera::create(std::move(cameraProperties));
    auto product = flux::RenderProduct::create(camera, renderProductProperties(1, 1, 2));

    flux::EmbreeHandler handler(context);
    handler.render(*product, 2);
    handler.download(*product);

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
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));
    auto bsdf = context->create<flux::DiffuseBSDF>();
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

TEST(EmbreePipelineTests, RendersDirectLightIntoColorChannel) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));
    auto bsdf = context->create<flux::DiffuseBSDF>();
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, *bsdf));

    kira::Properties lightProperties;
    lightProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    lightProperties.set("intensity", flux::Spectrum{1.0F, 1.0F, 1.0F});
    (void)context->create<flux::PointLight>(std::move(lightProperties));

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    cameraProperties.set("fov", 1.0F);
    auto camera = flux::Camera::create(std::move(cameraProperties));
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
}

TEST(EmbreePipelineTests, InvalidatesAccumulationForCameraFilmAndSync) {
    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
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
