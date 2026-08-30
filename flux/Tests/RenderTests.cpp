#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "TestUtils.h"
#include "flux/Embree/EmbreeHandler.h"
#include "flux/FLux/FLuxCLI.h"
#include "flux/FLux/TomlScene.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Film.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

namespace {
void render(flux::LoadedScene &scene, flux::RenderBackend backend) {
    auto const spp = scene.product->getSamplesPerPixel();
    if (backend == flux::RenderBackend::Embree) {
        flux::EmbreeHandler handler{scene.context};
        handler.render(*scene.product, spp);
        handler.download(*scene.product);
        return;
    }

    flux::OptixHandler handler{scene.context, std::filesystem::path{FLUX_TEST_OPTIX_IR}};
    auto remaining = spp;
    constexpr auto batchSize = 32U;
    while (remaining != 0) {
        auto const batch = std::min(remaining, batchSize);
        handler.render(*scene.product, batch);
        remaining -= batch;
    }
    handler.download(*scene.product);
}

[[nodiscard]] char const *
backendName(testing::TestParamInfo<flux::RenderBackend> const &info) noexcept {
    switch (info.param) {
    case flux::RenderBackend::Embree: return "Embree";
    case flux::RenderBackend::Optix: return "Optix";
    }
    return "Unknown";
}

class RenderTests : public testing::TestWithParam<flux::RenderBackend> {};
} // namespace

TEST_P(RenderTests, RendersWhiteDiffuseSphereInWhiteEnvironment) {
    auto const backend = GetParam();
    if (backend == flux::RenderBackend::Optix && !flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto scene = flux::loadTomlScene({
        .scenePath =
            std::filesystem::path{FLUX_TEST_FIXTURES_DIR} / "WhiteSphere" / "WhiteSphere.toml",
        .backend = backend,
    });
    scene.product->getFilm().setChannels(flux::FilmChannels::Color | flux::FilmChannels::Albedo);
    render(scene, backend);

    auto const color = scene.product->getFilm().getChannel<flux::ColorChannel>();
    auto const albedo = scene.product->getFilm().getChannel<flux::AlbedoChannel>();
    ASSERT_EQ(color.size(), 64U * 64U);
    ASSERT_EQ(albedo.size(), color.size());

    double absErr = 0.0;
    double sqErr = 0.0;
    std::size_t numSurfacePixels = 0;
    for (std::size_t pixel = 0; pixel < color.size(); ++pixel) {
        if (albedo[pixel].x() <= 0.5F)
            continue;

        ++numSurfacePixels;
        for (std::size_t component = 0; component < 3; ++component) {
            auto const error = static_cast<double>(color[pixel][component]) - 1.0;
            absErr += std::abs(error);
            sqErr += error * error;
        }
    }

    ASSERT_GT(numSurfacePixels, color.size() * 9U / 10U);
    auto const numComponents = static_cast<double>(numSurfacePixels * 3U);
    auto const mae = absErr / numComponents;
    auto const rmse = std::sqrt(sqErr / numComponents);
    EXPECT_LT(mae, 0.02);
    EXPECT_LT(rmse, 0.025);
}

INSTANTIATE_TEST_SUITE_P(
    Backends, RenderTests, testing::Values(flux::RenderBackend::Embree, flux::RenderBackend::Optix),
    backendName
);
