#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "flux/Scene/Context.h"
#include "flux/Scene/ShaderAsset.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Properties.h"

TEST(ShaderAssetTests, SharesOneAssetForEachSource) {
    flux::ShaderAssetPool pool;

    auto const asset = pool.getOrCreate({.source = "a"});

    EXPECT_EQ(pool.getOrCreate({.source = "a"}), asset);
    EXPECT_NE(pool.getOrCreate({.source = "b"}), asset);
}

TEST(ShaderAssetTests, RejectsATextureOfAnotherContext) {
    kira::Properties properties;
    properties.set(
        "path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Shared/Textures/Checker.png"
    );
    auto other = flux::Context::create();
    auto const texture = other->create<flux::ImageTexture>(properties);
    auto context = flux::Context::create();
    flux::ShaderAssetSpec const spec{.source = "a"};

    EXPECT_THROW(
        (void)context->create<flux::ShaderBSDF>(
            spec, std::vector<std::uint32_t>{},
            std::vector<flux::Ref<flux::ImageTexture const>>{texture}
        ),
        std::invalid_argument
    );
}

TEST(ShaderAssetTests, ShadesABSDFAndAnEDFFromOneAsset) {
    auto context = flux::Context::create();
    flux::ShaderAssetSpec const spec{.source = "a"};

    auto const bsdf = context->create<flux::ShaderBSDF>(spec, std::vector<std::uint32_t>{});
    auto const edf = context->create<flux::ShaderEDF>(spec, std::vector<std::uint32_t>{});

    EXPECT_EQ(bsdf->getAsset(), edf->getAsset());
}

TEST(ShaderAssetTests, EstimatesZeroLuminanceForAShaderEDFOnlyIfItCannotEmit) {
    auto context = flux::Context::create();
    auto const emits = context->create<flux::ShaderEDF>(
        flux::ShaderAssetSpec{.source = "emits"}, std::vector<std::uint32_t>{}
    );
    auto const emitsNothing = context->create<flux::ShaderEDF>(
        flux::ShaderAssetSpec{.source = "emitsNothing", .mayEmit = false},
        std::vector<std::uint32_t>{}
    );

    EXPECT_EQ(emits->estimateLuminance(), 1.0F);
    EXPECT_EQ(emitsNothing->estimateLuminance(), 0.0F);
}
