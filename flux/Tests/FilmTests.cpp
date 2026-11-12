#include <gtest/gtest.h>

#include <algorithm>
#include <type_traits>
#include <utility>

#include "flux/Scene/Film.h"

TEST(FilmTests, MovesAndSwapsFilmResolutionChannelsAndValues) {
    static_assert(!std::is_copy_constructible_v<flux::Film>);
    static_assert(!std::is_copy_assignable_v<flux::Film>);
    static_assert(std::is_nothrow_move_constructible_v<flux::Film>);
    static_assert(std::is_nothrow_move_assignable_v<flux::Film>);
    static_assert(std::is_nothrow_swappable_v<flux::Film>);

    flux::Film first(flux::FilmSpec{flux::Vec2u{1, 2}, flux::FilmChannels::Normal});
    flux::Film second(flux::FilmSpec{flux::Vec2u{3, 4}, flux::FilmChannels::Albedo});

    swap(first, second);
    EXPECT_EQ(first.getWidth(), 3);
    EXPECT_EQ(first.getHeight(), 4);
    EXPECT_EQ(first.getSpec().channels, flux::FilmChannels::Albedo);
    EXPECT_EQ(first.getChannel<flux::AlbedoChannel>().size(), 12);
    EXPECT_TRUE(first.getChannel<flux::NormalChannel>().empty());
    EXPECT_EQ(second.getWidth(), 1);
    EXPECT_EQ(second.getHeight(), 2);
    EXPECT_EQ(second.getSpec().channels, flux::FilmChannels::Normal);
    EXPECT_EQ(second.getChannel<flux::NormalChannel>().size(), 2);
    EXPECT_TRUE(second.getChannel<flux::AlbedoChannel>().empty());

    first = flux::Film(flux::FilmSpec{flux::Vec2u{5, 6}, flux::FilmChannels::All});
    EXPECT_EQ(first.getWidth(), 5);
    EXPECT_EQ(first.getHeight(), 6);
    EXPECT_EQ(first.getSpec().channels, flux::FilmChannels::All);
    EXPECT_EQ(first.getChannel<flux::ColorChannel>().size(), 30);
}

TEST(FilmTests, StoresZeroedValuesForRequestedFilmChannels) {
    flux::Film film(
        flux::FilmSpec{flux::Vec2u{2, 3}, flux::FilmChannels::Normal | flux::FilmChannels::Color}
    );

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

TEST(FilmTests, HoldsNoChannelsAfterMove) {
    flux::Film film(flux::FilmSpec{flux::Vec2u{2, 2}, flux::FilmChannels::All});
    flux::Film const moved{std::move(film)};

    EXPECT_EQ(moved.getSpec().channels, flux::FilmChannels::All);
    EXPECT_EQ(film.getSpec().channels, flux::FilmChannels::None); // NOLINT(bugprone-use-after-move)
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Color));
    EXPECT_TRUE(film.getChannel<flux::ColorChannel>().empty());
    EXPECT_TRUE(film.getChannel<flux::NormalChannel>().empty());
    EXPECT_TRUE(film.getChannel<flux::AlbedoChannel>().empty());
}

TEST(FilmTests, ReportsHeldFilmChannels) {
    flux::Film film(flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::Normal});
    EXPECT_TRUE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Albedo));

    film = flux::Film(flux::FilmSpec{flux::Vec2u{1, 1}, flux::FilmChannels::None});
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::None));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Normal));
    EXPECT_FALSE(film.hasChannel(flux::FilmChannels::Albedo));
    EXPECT_TRUE(film.getChannel<flux::NormalChannel>().empty());
}
