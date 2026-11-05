#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "flux/Scene/Context.h"
#include "flux/Scene/ShaderAsset.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Anyhow.h"
#include "kira/Properties.h"

TEST(ShaderAssetTests, SharesOneAssetForEachSource) {
    flux::ShaderAssetPool pool;

    auto const asset = pool.getOrCreate({.source = "a"});

    EXPECT_EQ(pool.getOrCreate({.source = "a"}), asset);
    EXPECT_NE(pool.getOrCreate({.source = "b"}), asset);
}

TEST(ShaderAssetTests, RejectsWordsThatDoNotFillTheParameters) {
    auto context = flux::Context::create();
    flux::ShaderAssetSpec const spec{.source = "a", .wordCount = 3};

    EXPECT_THROW(
        (void)context->create<flux::ShaderBSDF>(spec, std::vector<std::uint32_t>(2)), kira::Anyhow
    );
    EXPECT_NO_THROW((void)context->create<flux::ShaderBSDF>(spec, std::vector<std::uint32_t>(3)));
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
