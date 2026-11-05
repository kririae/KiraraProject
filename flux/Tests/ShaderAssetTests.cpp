#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "flux/Scene/Context.h"
#include "flux/Scene/ShaderAsset.h"
#include "flux/Shading/BSDF.h"
#include "kira/Anyhow.h"

TEST(ShaderAssetTests, SharesOneAssetForEachSource) {
    flux::ShaderAssetPool pool;

    auto const asset = pool.getOrCreate({.source = "a"});

    EXPECT_EQ(pool.getOrCreate({.source = "a"}), asset);
    EXPECT_NE(pool.getOrCreate({.source = "b"}), asset);
}

TEST(ShaderAssetTests, RejectsWordsThatDoNotFillTheParameters) {
    auto context = flux::Context::create();
    flux::ShaderAssetSpec const spec{
        .source = "a",
        .parameters = {{.path = "p", .type = "color3", .wordCount = 3, .offset = 0}},
    };

    EXPECT_THROW(
        (void)context->create<flux::ShaderBSDF>(spec, std::vector<std::uint32_t>(2)), kira::Anyhow
    );
    EXPECT_NO_THROW((void)context->create<flux::ShaderBSDF>(spec, std::vector<std::uint32_t>(3)));
}
