#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <type_traits>

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

TEST(RenderProductTests, RejectsDegenerateCameraFrame) {
    auto camera = flux::Camera::create();
    EXPECT_THROW(camera->setVerticalFieldOfView(180.0F), kira::Anyhow);

    camera->setPosition({std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    EXPECT_THROW((void)camera->getImpl(), kira::Anyhow);

    camera->setPosition({1.0F, 2.0F, 3.0F});
    camera->setLookAt({1.0F, 2.0F, 3.0F});

    EXPECT_THROW((void)camera->getImpl(), kira::Anyhow);
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

    product->getFilm().setResolution(1280, 720);

    EXPECT_EQ(product->getFilm().getWidth(), 1280U);
    EXPECT_EQ(product->getFilm().getHeight(), 720U);

    product->getFilm().setResolution(320, 240);

    EXPECT_EQ(product->getFilm().getWidth(), 320U);
    EXPECT_EQ(product->getFilm().getHeight(), 240U);

    product->setSamplesPerPixel(1);
    EXPECT_EQ(product->getSamplesPerPixel(), 1U);
    product->setSamplesPerPixel(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(product->getSamplesPerPixel(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_THROW(product->setSamplesPerPixel(0), kira::Anyhow);
    EXPECT_EQ(product->getSamplesPerPixel(), std::numeric_limits<std::uint32_t>::max());
}

TEST(RenderProductTests, RejectsZeroFilmDimensions) {
    EXPECT_THROW((void)flux::Film(0, 1), kira::Anyhow);

    flux::Film film(1, 1);
    EXPECT_THROW(film.setResolution(1, 0), kira::Anyhow);
    EXPECT_EQ(film.getWidth(), 1U);
    EXPECT_EQ(film.getHeight(), 1U);
}

TEST(RenderProductTests, MovesAndSwapsFilmResolutionAndChannels) {
    static_assert(!std::is_copy_constructible_v<flux::Film>);
    static_assert(!std::is_copy_assignable_v<flux::Film>);
    static_assert(std::is_nothrow_move_constructible_v<flux::Film>);
    static_assert(std::is_nothrow_move_assignable_v<flux::Film>);
    static_assert(std::is_nothrow_swappable_v<flux::Film>);

    flux::Film first(1, 2);
    first.setChannels(flux::FilmChannels::Normal);
    flux::Film second(3, 4);
    second.setChannels(flux::FilmChannels::Albedo);

    swap(first, second);
    EXPECT_EQ(first.getWidth(), 3);
    EXPECT_EQ(first.getHeight(), 4);
    EXPECT_EQ(first.getChannels(), flux::FilmChannels::Albedo);
    EXPECT_EQ(second.getWidth(), 1);
    EXPECT_EQ(second.getHeight(), 2);
    EXPECT_EQ(second.getChannels(), flux::FilmChannels::Normal);

    first = flux::Film(5, 6);
    EXPECT_EQ(first.getWidth(), 5);
    EXPECT_EQ(first.getHeight(), 6);
    EXPECT_EQ(first.getChannels(), flux::FilmChannels::All);
}

TEST(RenderProductTests, SetsRequestedFilmChannels) {
    flux::Film film(1, 1);
    EXPECT_TRUE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_TRUE(film.hasChannel(flux::FilmChannels::Albedo));

    film.setChannels(flux::FilmChannels::Normal);
    EXPECT_TRUE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Albedo));

    film.setChannels(flux::FilmChannels::None);
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::None));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Albedo));

    // Use an underlying value that has no FilmChannels enumerator.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    auto const unknownChannels = static_cast<flux::FilmChannels>(1U << 31U);
    EXPECT_THROW(film.setChannels(unknownChannels), kira::Anyhow);
    EXPECT_EQ(film.getChannels(), flux::FilmChannels::None);
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
