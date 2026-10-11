#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "CropUtils.h"
#include "flux/Core/MathUtils.h"
#include "flux/Core/RenderStats.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/CameraImpl.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/RenderProduct.h"

TEST(RenderStatsTests, ReportsCameraPathRate) {
    auto stats = flux::RenderStats{
        .paths = 10,
        .elapsed = std::chrono::seconds{2},
    };
    EXPECT_DOUBLE_EQ(stats.getPathsPerSecond(), 5.0);

    stats.elapsed = {};
    EXPECT_DOUBLE_EQ(stats.getPathsPerSecond(), 0.0);
}

TEST(RenderProductTests, BuildsPinholeCameraFrame) {
    kira::Properties properties;
    properties.set("position", flux::Vec3f{0.0F, 0.0F, 0.0F});
    properties.set("look_at", flux::Vec3f{0.0F, 0.0F, -1.0F});
    properties.set("ref_up", flux::Vec3f{0.0F, 1.0F, 0.0F});
    properties.set("fov", 90.0F);
    auto camera = flux::Camera::create(properties);

    auto const impl = camera->getImpl();

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_FLOAT_EQ(impl.position.x(), 0.0F);
    EXPECT_FLOAT_EQ(impl.forward.z(), -1.0F);
    EXPECT_FLOAT_EQ(impl.right.x(), 1.0F);
    EXPECT_FLOAT_EQ(impl.up.y(), 1.0F);
    EXPECT_NEAR(impl.halfHeight, 1.0F, 1.0e-6F);

    auto const ray = impl.generateRay({320.0F, 240.0F}, {0.5F, 0.5F}, {640U, 480U});
    EXPECT_EQ(ray.origin, impl.position);
    EXPECT_EQ(ray.direction, impl.forward);
}

TEST(RenderProductTests, ShowsTheWholeViewByDefault) {
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

TEST(RenderProductTests, GeneratesTheRaysOfACropWithAnOffCenterScreenWindow) {
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

TEST(RenderProductTests, FocusesThinLensFootprintsAtTheFocusPlane) {
    kira::Properties props;
    props.set("lens_radius", 0.25F);
    props.set("focal_distance", 4.0F);
    auto const camera = flux::Camera::create(props)->getImpl();

    auto footprint = camera.getRayFootprint(camera.forward, {640U, 480U});
    footprint.propagate(camera.focalDistance);

    EXPECT_NEAR(footprint.cones[1].width, 0.0F, 1.0e-7F);
}

TEST(RenderProductTests, BoundsTheFieldOfViewToAFiniteProjection) {
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

TEST(RenderProductTests, DisablesDepthOfFieldForAnInvalidLens) {
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

TEST(RenderProductTests, GivesTheSameLensInAnyOrderOfSetters) {
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

TEST(RenderProductTests, KeepsTheViewForADegenerateReferenceUp) {
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

TEST(RenderProductTests, FallsBackToTheDefaultFrameForAZeroView) {
    auto camera = flux::Camera::create();
    camera->setPosition({1.0F, 2.0F, 3.0F});
    camera->setLookAt({1.0F, 2.0F, 3.0F});
    auto const impl = camera->getImpl();
    EXPECT_EQ(impl.forward, (flux::Vec3f{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(impl.right, (flux::Vec3f{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(impl.up, (flux::Vec3f{0.0F, 1.0F, 0.0F}));
    EXPECT_EQ(impl.position, (flux::Vec3f{1.0F, 2.0F, 3.0F}));
}

TEST(RenderProductTests, OwnsFilmAndSampleTarget) {
    auto camera = flux::Camera::create();
    kira::Properties properties;
    properties.set("resolution", flux::Vec2u{640, 480});
    properties.set("num_samples", std::uint32_t{16});
    auto product = flux::RenderProduct::create(camera, properties);

    EXPECT_TRUE(properties.is_all_used());
    EXPECT_EQ(&product->getCamera(), camera.get());
    EXPECT_EQ(product->getFilm().getWidth(), 640U);
    EXPECT_EQ(product->getFilm().getHeight(), 480U);
    EXPECT_EQ(product->getSamplesPerPixel(), 16U);

    product->getFilm() = flux::Film{flux::Vec2u{1280, 720}, flux::FilmChannels::Color};

    EXPECT_EQ(product->getFilm().getWidth(), 1280U);
    EXPECT_EQ(product->getFilm().getHeight(), 720U);

    EXPECT_EQ(product->getFilm().getChannels(), flux::FilmChannels::Color);

    product->setSamplesPerPixel(1);
    EXPECT_EQ(product->getSamplesPerPixel(), 1U);
    product->setSamplesPerPixel(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(product->getSamplesPerPixel(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_THROW(product->setSamplesPerPixel(0), kira::Anyhow);
    EXPECT_EQ(product->getSamplesPerPixel(), std::numeric_limits<std::uint32_t>::max());
}

TEST(RenderProductTests, MovesAndSwapsFilmResolutionChannelsAndValues) {
    static_assert(!std::is_copy_constructible_v<flux::Film>);
    static_assert(!std::is_copy_assignable_v<flux::Film>);
    static_assert(std::is_nothrow_move_constructible_v<flux::Film>);
    static_assert(std::is_nothrow_move_assignable_v<flux::Film>);
    static_assert(std::is_nothrow_swappable_v<flux::Film>);

    flux::Film first(flux::Vec2u{1, 2}, flux::FilmChannels::Normal);
    flux::Film second(flux::Vec2u{3, 4}, flux::FilmChannels::Albedo);

    swap(first, second);
    EXPECT_EQ(first.getWidth(), 3);
    EXPECT_EQ(first.getHeight(), 4);
    EXPECT_EQ(first.getChannels(), flux::FilmChannels::Albedo);
    EXPECT_EQ(first.getChannel<flux::AlbedoChannel>().size(), 12);
    EXPECT_TRUE(first.getChannel<flux::NormalChannel>().empty());
    EXPECT_EQ(second.getWidth(), 1);
    EXPECT_EQ(second.getHeight(), 2);
    EXPECT_EQ(second.getChannels(), flux::FilmChannels::Normal);
    EXPECT_EQ(second.getChannel<flux::NormalChannel>().size(), 2);
    EXPECT_TRUE(second.getChannel<flux::AlbedoChannel>().empty());

    first = flux::Film(flux::Vec2u{5, 6}, flux::FilmChannels::All);
    EXPECT_EQ(first.getWidth(), 5);
    EXPECT_EQ(first.getHeight(), 6);
    EXPECT_EQ(first.getChannels(), flux::FilmChannels::All);
    EXPECT_EQ(first.getChannel<flux::ColorChannel>().size(), 30);
}

TEST(RenderProductTests, StoresZeroedValuesForRequestedFilmChannels) {
    flux::Film film(flux::Vec2u{2, 3}, flux::FilmChannels::Normal | flux::FilmChannels::Color);

    auto const normal = film.getChannel<flux::NormalChannel>();
    ASSERT_EQ(normal.size(), 6);
    EXPECT_TRUE(std::ranges::all_of(normal, [](auto const &value) {
        return value == flux::Vec3f{};
    }));
    auto const color = film.getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 6);
    EXPECT_TRUE(std::ranges::all_of(color, [](auto const &value) {
        return value == flux::Spectrum{};
    }));
    EXPECT_TRUE(film.getChannel<flux::AlbedoChannel>().empty());
}

TEST(RenderProductTests, HoldsNoChannelsAfterMove) {
    flux::Film film(flux::Vec2u{2, 2}, flux::FilmChannels::All);
    flux::Film const moved{std::move(film)};

    EXPECT_EQ(moved.getChannels(), flux::FilmChannels::All);
    EXPECT_EQ(film.getChannels(), flux::FilmChannels::None); // NOLINT(bugprone-use-after-move)
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Color));
    EXPECT_TRUE(film.getChannel<flux::ColorChannel>().empty());
    EXPECT_TRUE(film.getChannel<flux::NormalChannel>().empty());
    EXPECT_TRUE(film.getChannel<flux::AlbedoChannel>().empty());
}

TEST(RenderProductTests, ReportsHeldFilmChannels) {
    flux::Film film(flux::Vec2u{1, 1}, flux::FilmChannels::Normal);
    EXPECT_TRUE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Albedo));

    film = flux::Film(flux::Vec2u{1, 1}, flux::FilmChannels::None);
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::None));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Albedo));
    EXPECT_TRUE(film.getChannel<flux::NormalChannel>().empty());
}

TEST(RenderProductTests, AccumulatesFilmChannelsOnHost) {
    auto normal = std::array<flux::Vec3f, 1>{};
    auto film = flux::Film::Impl{.width = 1, .height = 1};
    film.channels.get<flux::NormalChannel>().data = normal.data();

    film.accumulate<flux::NormalChannel>({0, 0}, {1.0F, 2.0F, 4.0F});
    film.accumulate<flux::NormalChannel>({0, 0}, {2.0F, 3.0F, 5.0F});
    film.scale(0, 0.5F);

    EXPECT_EQ(normal[0], (flux::Vec3f{1.5F, 2.5F, 4.5F}));
    EXPECT_FALSE(film.hasChannel<flux::AlbedoChannel>());
}

TEST(RenderProductTests, SamplesTheDiskConcentrically) {
    EXPECT_EQ(flux::uniformSampleDisk({0.5F, 0.5F}), (flux::Vec2f{}));

    auto const sample = flux::uniformSampleDisk({0.75F, 0.5F});
    EXPECT_NEAR(sample.x(), 0.5F, 1.0e-6F);
    EXPECT_NEAR(sample.y(), 0.0F, 1.0e-6F);
}
