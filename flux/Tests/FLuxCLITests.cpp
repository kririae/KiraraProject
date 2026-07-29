#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "flux/FLuxCLI.h"
#include "kira/Anyhow.h"

namespace {
[[nodiscard]] flux::FluxCLIRequest parse(std::initializer_list<char const *> arguments) {
    std::vector<std::string> storage;
    storage.reserve(arguments.size());
    for (auto const *argument : arguments)
        storage.emplace_back(argument);

    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (auto &argument : storage)
        argv.push_back(argument.data());
    return flux::parseFluxCLI(static_cast<int>(argv.size()), argv.data());
}
} // namespace

TEST(FLuxCLITests, UsesStableDefaults) {
    auto const request = parse({"flux", "scene.toml"});

    EXPECT_EQ(request.scenePath, "scene.toml");
    EXPECT_FALSE(request.outputPath);
    EXPECT_FALSE(request.samplesPerPixel);
    EXPECT_EQ(request.backend, flux::RenderBackend::Optix);
}

TEST(FLuxCLITests, ParsesRenderOverrides) {
    auto const request = parse({
        "flux",
        "scene.toml",
        "--output",
        "images/result.exr",
        "--backend",
        "embree",
        "--spp",
        "16",
    });

    EXPECT_EQ(request.outputPath, "images/result.exr");
    EXPECT_EQ(request.samplesPerPixel, 16);
    EXPECT_EQ(request.backend, flux::RenderBackend::Embree);
}

TEST(FLuxCLITests, RejectsUnknownBackend) {
    EXPECT_THROW((void)parse({"flux", "scene.toml", "--backend", "cuda"}), kira::Anyhow);
}

TEST(FLuxCLITests, RejectsZeroSamples) {
    EXPECT_THROW((void)parse({"flux", "scene.toml", "--spp", "0"}), kira::Anyhow);
}
