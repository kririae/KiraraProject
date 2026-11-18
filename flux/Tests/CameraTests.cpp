#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "CropUtils.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/CameraImpl.h"

TEST(CameraTests, BuildsPinholeCameraFrame) {
    kira::Properties properties;
    properties.set("position", flux::Vec3f{0.0F, 0.0F, 0.0F});
    properties.set("look_at", flux::Vec3f{0.0F, 0.0F, -1.0F});
    properties.set("ref_up", flux::Vec3f{1.0F, 0.0F, 0.0F});
    properties.set("fov", 90.0F);
    auto camera = flux::Camera::create(properties);

    auto const impl = camera->getImpl();

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_FLOAT_EQ(impl.position.x(), 0.0F);
    EXPECT_FLOAT_EQ(impl.forward.z(), -1.0F);
    EXPECT_FLOAT_EQ(impl.right.y(), -1.0F);
    EXPECT_FLOAT_EQ(impl.up.x(), 1.0F);
    EXPECT_NEAR(impl.halfHeight, 1.0F, 1.0e-6F);

    auto const ray = impl.generateRay({320.0F, 240.0F}, {0.5F, 0.5F}, {640U, 480U});
    EXPECT_EQ(ray.origin, impl.position);
    EXPECT_EQ(ray.direction, impl.forward);
}

TEST(CameraTests, ShowsTheWholeViewByDefault) {
    kira::Properties properties;
    properties.set("fov", 50.0F);
    properties.set("position", flux::Vec3f{1.0F, 2.0F, 3.0F});
    auto camera = flux::Camera::create(properties);
    EXPECT_EQ(camera->getScreenWindow(), (flux::ScreenWindow{-1.0F, 1.0F, -1.0F, 1.0F}));

    // The default window maps the film to the whole view.
    flux::Vec2u const resolution{640U, 480U};
    auto const impl = camera->getImpl();
    auto const inverseWidth = 1.0F / 640.0F;
    auto const inverseHeight = 1.0F / 480.0F;
    for (auto const raster : {flux::Vec2f{0.0F, 0.0F}, {320.0F, 240.0F}, {123.5F, 400.25F}}) {
        auto const screenX = raster.x() * inverseWidth * 2.0F - 1.0F;
        auto const screenY = 1.0F - raster.y() * inverseHeight * 2.0F;
        auto const aspect = 640.0F * inverseHeight;
        auto const direction = impl.forward + impl.right * (screenX * impl.halfHeight * aspect) +
                               impl.up * (screenY * impl.halfHeight);
        EXPECT_EQ(
            impl.generateRay(raster, {0.5F, 0.5F}, resolution).direction,
            direction / direction.norm()
        );
    }
}

TEST(CameraTests, GeneratesTheRaysOfACropWithAnOffCenterScreenWindow) {
    flux::Vec2u const fullResolution{16U, 12U};
    flux::Vec2u const offset{4U, 3U};
    flux::Vec2u const size{8U, 6U};

    kira::Properties properties;
    properties.set("position", flux::Vec3f{0.5F, 0.25F, 2.0F});
    properties.set("look_at", flux::Vec3f{0.0F, 0.0F, 0.0F});
    properties.set("lens_radius", 0.1F);
    properties.set("focal_distance", 3.0F);
    auto full = flux::Camera::create(properties);
    auto crop = flux::Camera::create(properties);
    crop->setScreenWindow(flux::test::cropWindow(fullResolution, offset, size));
    auto const fullImpl = full->getImpl();
    auto const cropImpl = crop->getImpl();

    for (std::uint32_t y = 0; y < size.y(); ++y) {
        for (std::uint32_t x = 0; x < size.x(); ++x) {
            SCOPED_TRACE(::testing::Message() << x << ", " << y);
            auto const lensSample = flux::Vec2f{0.3F, 0.7F};
            auto const expected = fullImpl.generateRay(
                {static_cast<float>(offset.x() + x) + 0.25F,
                 static_cast<float>(offset.y() + y) + 0.75F},
                lensSample, fullResolution
            );
            auto const actual = cropImpl.generateRay(
                {static_cast<float>(x) + 0.25F, static_cast<float>(y) + 0.75F}, lensSample, size
            );
            EXPECT_LT((actual.origin - expected.origin).norm(), 1.0e-6F);
            EXPECT_LT((actual.direction - expected.direction).norm(), 1.0e-5F);
        }
    }

    auto const expectedFootprint = fullImpl.getRayFootprint(fullImpl.forward, fullResolution);
    auto const actualFootprint = cropImpl.getRayFootprint(fullImpl.forward, size);
    EXPECT_NEAR(actualFootprint.cones[0].angle, expectedFootprint.cones[0].angle, 1.0e-7F);
}

TEST(CameraTests, FocusesThinLensFootprintsAtTheFocusPlane) {
    kira::Properties props;
    props.set("lens_radius", 0.25F);
    props.set("focal_distance", 4.0F);
    auto const camera = flux::Camera::create(props)->getImpl();

    auto footprint = camera.getRayFootprint(camera.forward, {640U, 480U});
    footprint.propagate(camera.focalDistance);

    EXPECT_NEAR(footprint.cones[1].width, 0.0F, 1.0e-7F);
}

TEST(CameraTests, BoundsTheFieldOfViewToAFiniteProjection) {
    auto camera = flux::Camera::create();
    for (auto const degrees : {-5.0F, 0.0F, 180.0F, 1.0e6F}) {
        camera->setVerticalFieldOfView(degrees);
        EXPECT_GT(camera->getVerticalFieldOfView(), 0.0F);
        EXPECT_LT(camera->getVerticalFieldOfView(), 180.0F);
        auto const halfHeight = camera->getImpl().halfHeight;
        EXPECT_GT(halfHeight, 0.0F);
        EXPECT_TRUE(std::isfinite(halfHeight));
    }

    camera->setVerticalFieldOfView(90.0F);
    EXPECT_EQ(camera->getVerticalFieldOfView(), 90.0F);

    kira::Properties properties;
    properties.set("fov", 180.0F);
    EXPECT_LT(flux::Camera::create(properties)->getVerticalFieldOfView(), 180.0F);
}

TEST(CameraTests, DisablesDepthOfFieldForAnInvalidLens) {
    auto camera = flux::Camera::create();
    camera->setLensRadius(0.25F);
    camera->setFocalDistance(4.0F);
    EXPECT_EQ(camera->getImpl().lensRadius, 0.25F);
    EXPECT_EQ(camera->getImpl().focalDistance, 4.0F);

    camera->setLensRadius(-1.0F);
    EXPECT_EQ(camera->getLensRadius(), 0.0F);
    EXPECT_EQ(camera->getImpl().lensRadius, 0.0F);

    camera->setLensRadius(0.25F);
    camera->setFocalDistance(-4.0F);
    EXPECT_EQ(camera->getFocalDistance(), 0.0F);
    EXPECT_EQ(camera->getImpl().lensRadius, 0.0F);
}

TEST(CameraTests, GivesTheSameLensInAnyOrderOfSetters) {
    auto const lens = [](float radius, float focalDistance, bool radiusFirst) {
        auto camera = flux::Camera::create();
        if (radiusFirst) {
            camera->setLensRadius(radius);
            camera->setFocalDistance(focalDistance);
        } else {
            camera->setFocalDistance(focalDistance);
            camera->setLensRadius(radius);
        }
        auto const impl = camera->getImpl();
        return std::pair{impl.lensRadius, impl.focalDistance};
    };
    for (auto const radius : {-1.0F, 0.0F, 0.25F}) {
        for (auto const focalDistance : {-4.0F, 0.0F, 4.0F}) {
            SCOPED_TRACE(::testing::Message() << radius << ", " << focalDistance);
            EXPECT_EQ(lens(radius, focalDistance, true), lens(radius, focalDistance, false));
        }
    }
    EXPECT_EQ(lens(0.25F, 4.0F, true), (std::pair{0.25F, 4.0F}));
    EXPECT_EQ(lens(0.25F, 0.0F, false), (std::pair{0.0F, 0.0F}));
}

TEST(CameraTests, KeepsTheViewForADegenerateReferenceUp) {
    auto const expectOrthonormalFrame = [](flux::Camera::Impl const &impl) {
        EXPECT_NEAR(impl.forward.norm(), 1.0F, 1.0e-6F);
        EXPECT_NEAR(impl.right.norm(), 1.0F, 1.0e-6F);
        EXPECT_NEAR(impl.up.norm(), 1.0F, 1.0e-6F);
        EXPECT_NEAR(impl.right.dot(impl.forward), 0.0F, 1.0e-6F);
        EXPECT_NEAR(impl.up.dot(impl.forward), 0.0F, 1.0e-6F);
        EXPECT_NEAR(impl.right.dot(impl.up), 0.0F, 1.0e-6F);
        EXPECT_LT((flux::cross(impl.right, impl.up) + impl.forward).norm(), 1.0e-6F);
    };

    // The view looks along +Y, parallel to the default reference up.
    auto camera = flux::Camera::create();
    camera->setPosition({1.0F, 2.0F, 3.0F});
    camera->setLookAt({1.0F, 12.0F, 3.0F});
    auto impl = camera->getImpl();
    EXPECT_EQ(impl.forward, (flux::Vec3f{0.0F, 1.0F, 0.0F}));
    expectOrthonormalFrame(impl);

    // A zero reference up keeps the view too.
    camera->setLookAt({4.0F, 6.0F, 3.0F});
    camera->setReferenceUp({});
    impl = camera->getImpl();
    EXPECT_LT((impl.forward - flux::Vec3f{0.6F, 0.8F, 0.0F}).norm(), 1.0e-6F);
    expectOrthonormalFrame(impl);

    // A regular reference up gives the usual look-at axes.
    camera->setReferenceUp({0.0F, 1.0F, 0.0F});
    camera->setLookAt({1.0F, 2.0F, 0.0F});
    impl = camera->getImpl();
    EXPECT_EQ(impl.right, (flux::Vec3f{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(impl.up, (flux::Vec3f{0.0F, 1.0F, 0.0F}));
}

TEST(CameraTests, FallsBackToTheDefaultFrameForAZeroView) {
    auto camera = flux::Camera::create();
    camera->setPosition({1.0F, 2.0F, 3.0F});
    camera->setLookAt({1.0F, 2.0F, 3.0F});
    auto const impl = camera->getImpl();
    EXPECT_EQ(impl.forward, (flux::Vec3f{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(impl.right, (flux::Vec3f{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(impl.up, (flux::Vec3f{0.0F, 1.0F, 0.0F}));
    EXPECT_EQ(impl.position, (flux::Vec3f{1.0F, 2.0F, 3.0F}));
}
