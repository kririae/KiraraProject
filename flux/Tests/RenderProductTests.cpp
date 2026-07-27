#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/RenderProduct.h"

TEST(RenderProductTests, MaterializesPinholeCameraFrame) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("position", flux::Vec3f{0.0F, 0.0F, 0.0F});
    properties.set("look_at", flux::Vec3f{0.0F, 0.0F, -1.0F});
    properties.set("ref_up", flux::Vec3f{0.0F, 1.0F, 0.0F});
    properties.set("fov", 90.0F);
    auto camera = context->create<flux::Camera>(std::move(properties));

    auto const device = camera->getDeviceImpl();

    EXPECT_TRUE(camera->getProperties().is_all_used());
    EXPECT_FLOAT_EQ(device.position.x(), 0.0F);
    EXPECT_FLOAT_EQ(device.forward.z(), -1.0F);
    EXPECT_FLOAT_EQ(device.right.x(), 1.0F);
    EXPECT_FLOAT_EQ(device.up.y(), 1.0F);
    EXPECT_NEAR(device.halfHeight, 1.0F, 1.0e-6F);
}

TEST(RenderProductTests, RejectsDegenerateCameraFrame) {
    auto context = flux::Context::create();
    auto camera = context->create<flux::Camera>();
    EXPECT_THROW(camera->setVerticalFieldOfView(180.0F), kira::Anyhow);

    camera->setPosition({std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    EXPECT_THROW((void)camera->getDeviceImpl(), kira::Anyhow);

    camera->setPosition({1.0F, 2.0F, 3.0F});
    camera->setLookAt({1.0F, 2.0F, 3.0F});

    EXPECT_THROW((void)camera->getDeviceImpl(), kira::Anyhow);
}

TEST(RenderProductTests, OwnsResizableFilmDescriptor) {
    auto context = flux::Context::create();
    kira::Properties properties;
    properties.set("width", std::uint32_t{640});
    properties.set("height", std::uint32_t{480});
    auto product = context->create<flux::RenderProduct>(std::move(properties));

    EXPECT_TRUE(product->getProperties().is_all_used());
    EXPECT_EQ(product->getFilm().getWidth(), 640U);
    EXPECT_EQ(product->getFilm().getHeight(), 480U);

    product->getFilm().setResolution(1280, 720);

    EXPECT_EQ(product->getFilm().getWidth(), 1280U);
    EXPECT_EQ(product->getFilm().getHeight(), 720U);
}

TEST(RenderProductTests, RejectsZeroFilmDimensions) {
    EXPECT_THROW((void)flux::Film(0, 1), kira::Anyhow);

    flux::Film film(1, 1);
    EXPECT_THROW(film.setResolution(1, 0), kira::Anyhow);
    EXPECT_EQ(film.getWidth(), 1U);
    EXPECT_EQ(film.getHeight(), 1U);
}
