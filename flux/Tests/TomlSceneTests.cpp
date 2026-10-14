#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "flux/FLux/FLuxCLI.h"
#include "flux/FLux/TomlScene.h"

namespace {
/// Writes a scene with \p filmProperties in its film table and returns its path.
[[nodiscard]] std::filesystem::path writeScene(std::string const &filmProperties) {
    auto const path = std::filesystem::path{testing::TempDir()} / "flux-cli-scene.toml";
    std::ofstream output(path);
    output << "[camera]\n[integrator]\n[film]\nresolution = [4, 2]\n" << filmProperties;
    return path;
}
} // namespace

TEST(TomlSceneTests, LoadsTheFilmOfAScene) {
    auto options = flux::FluxCLIOptions{.scenePath = writeScene("num_samples = 16\n")};
    auto scene = flux::loadTomlScene(options);

    EXPECT_EQ(scene.film.resolution, (flux::Vec2u{4, 2}));
    EXPECT_EQ(scene.film.channels, flux::FilmChannels::Color);
    EXPECT_EQ(scene.samplesPerPixel, 16);

    options.samplesPerPixel = 4;
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 4);
}

TEST(TomlSceneTests, RendersOneSampleByDefault) {
    auto const options = flux::FluxCLIOptions{.scenePath = writeScene("")};
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 1);
}

TEST(TomlSceneTests, KeepsZeroSamples) {
    auto options = flux::FluxCLIOptions{.scenePath = writeScene("num_samples = 0\n")};
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 0);

    options.scenePath = writeScene("num_samples = 8\n");
    options.samplesPerPixel = 0;
    EXPECT_EQ(flux::loadTomlScene(options).samplesPerPixel, 0);
}
