#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "flux/Core/MathUtils.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/RenderProduct.h"

TEST(RenderProductTests, MaterializesPinholeCameraFrame) {
    kira::Properties properties;
    properties.set("position", flux::Vec3f{0.0F, 0.0F, 0.0F});
    properties.set("look_at", flux::Vec3f{0.0F, 0.0F, -1.0F});
    properties.set("ref_up", flux::Vec3f{0.0F, 1.0F, 0.0F});
    properties.set("fov", 90.0F);
    auto camera = flux::Camera::create(properties);

    auto const device = camera->getDeviceImpl();

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_FLOAT_EQ(device.position.x(), 0.0F);
    EXPECT_FLOAT_EQ(device.forward.z(), -1.0F);
    EXPECT_FLOAT_EQ(device.right.x(), 1.0F);
    EXPECT_FLOAT_EQ(device.up.y(), 1.0F);
    EXPECT_NEAR(device.halfHeight, 1.0F, 1.0e-6F);
}

TEST(RenderProductTests, RejectsDegenerateCameraFrame) {
    auto camera = flux::Camera::create();
    EXPECT_THROW(camera->setVerticalFieldOfView(180.0F), kira::Anyhow);

    camera->setPosition({std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    EXPECT_THROW((void)camera->getDeviceImpl(), kira::Anyhow);

    camera->setPosition({1.0F, 2.0F, 3.0F});
    camera->setLookAt({1.0F, 2.0F, 3.0F});

    EXPECT_THROW((void)camera->getDeviceImpl(), kira::Anyhow);
}

TEST(RenderProductTests, OwnsResizableFilmDescriptor) {
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("width", std::uint32_t{640});
    properties.set("height", std::uint32_t{480});
    properties.set("num_samples", std::uint32_t{16});
    auto product = flux::RenderProduct::create(camera, properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_EQ(&product->getCamera(), camera.get());
    EXPECT_EQ(product->getFilm().getWidth(), 640U);
    EXPECT_EQ(product->getFilm().getHeight(), 480U);
    EXPECT_EQ(product->getSamplesPerPixel(), 16U);

    product->getFilm().setResolution(1280, 720);

    EXPECT_EQ(product->getFilm().getWidth(), 1280U);
    EXPECT_EQ(product->getFilm().getHeight(), 720U);

    product->setSamplesPerPixel(32);
    EXPECT_EQ(product->getSamplesPerPixel(), 32U);
    EXPECT_THROW(product->setSamplesPerPixel(0), kira::Anyhow);
    EXPECT_EQ(product->getSamplesPerPixel(), 32U);
}

TEST(RenderProductTests, RejectsZeroFilmDimensions) {
    EXPECT_THROW((void)flux::Film(0, 1), kira::Anyhow);

    flux::Film film(1, 1);
    EXPECT_THROW(film.setResolution(1, 0), kira::Anyhow);
    EXPECT_EQ(film.getWidth(), 1U);
    EXPECT_EQ(film.getHeight(), 1U);
}

TEST(RenderProductTests, SamplesTheDiskConcentrically) {
    EXPECT_EQ(flux::uniformSampleDisk({0.5F, 0.5F}), (flux::Vec2f{}));

    auto const sample = flux::uniformSampleDisk({0.75F, 0.5F});
    EXPECT_NEAR(sample.x(), 0.5F, 1.0e-6F);
    EXPECT_NEAR(sample.y(), 0.0F, 1.0e-6F);
}
