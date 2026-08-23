#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/FLux/FLuxCLI.h"
#include "kira/Anyhow.h"
#include "kira/detail/Logger.h"

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
    EXPECT_FALSE(request.logFilePath);
    EXPECT_FALSE(request.samplesPerPixel);
    EXPECT_EQ(request.backend, flux::RenderBackend::Optix);
    EXPECT_EQ(request.logLevel, spdlog::level::warn);
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
        "--log-level",
        "debug",
        "--log-file",
        "logs/flux.log",
    });

    EXPECT_EQ(request.outputPath, "images/result.exr");
    EXPECT_EQ(request.logFilePath, "logs/flux.log");
    EXPECT_EQ(request.samplesPerPixel, 16);
    EXPECT_EQ(request.backend, flux::RenderBackend::Embree);
    EXPECT_EQ(request.logLevel, spdlog::level::debug);
}

TEST(FLuxCLITests, RejectsUnknownBackend) {
    EXPECT_THROW((void)parse({"flux", "scene.toml", "--backend", "cuda"}), kira::Anyhow);
}

TEST(FLuxCLITests, RejectsZeroSamples) {
    EXPECT_THROW((void)parse({"flux", "scene.toml", "--spp", "0"}), kira::Anyhow);
}

TEST(FLuxCLITests, RejectsUnknownLogLevel) {
    EXPECT_THROW((void)parse({"flux", "scene.toml", "--log-level", "verbose"}), kira::Anyhow);
}

TEST(FLuxCLITests, RejectsMissingLogLevel) {
    EXPECT_ANY_THROW((void)parse({"flux", "scene.toml", "--log-level"}));
}

TEST(FLuxCLITests, ConfiguresLevelAndFile) {
    auto const directory = std::filesystem::path{testing::TempDir()} / "flux-cli-logs";
    auto const path = directory / "flux.log";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    {
        std::ofstream output(path);
        output << "existing log\n";
    }

    flux::FluxCLIRequest request;
    request.logFilePath = path;
    request.logLevel = spdlog::level::info;
#if !defined(_WIN32)
    ::testing::internal::CaptureStderr();
#endif
    flux::configureLogging(request);
    flux::LogDebug("FLuxCLITests: hidden");
    flux::LogInfo("FLuxCLITests: message");
    kira::LogFlush<"flux">();
#if !defined(_WIN32)
    auto const console = ::testing::internal::GetCapturedStderr();
    EXPECT_NE(console.find("FLuxCLITests: message"), std::string::npos);
    EXPECT_EQ(console.find("FLuxCLITests: hidden"), std::string::npos);
#endif

    {
        std::ifstream input(path);
        auto const contents = std::string{std::istreambuf_iterator<char>{input}, {}};
        EXPECT_NE(contents.find("existing log"), std::string::npos);
        EXPECT_NE(contents.find("FLuxCLITests: message"), std::string::npos);
        EXPECT_EQ(contents.find("FLuxCLITests: hidden"), std::string::npos);
    }

    spdlog::drop("flux");
    (void)kira::detail::SinkManager::GetInstance().DropAllSinks();
    std::filesystem::remove_all(directory);
}
