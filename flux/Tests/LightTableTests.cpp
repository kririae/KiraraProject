#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "TestUtils.h"
#include "flux/Sampling/LightPowerDistribution.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/LightTableData.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTableData.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/EDF.h"

namespace {
[[nodiscard]] flux::TriangleMesh::Data triangle() {
    using flux::test::sharedBuffer;
    return {
        .vertices = sharedBuffer(
            flux::Vec3f{0.0F, 0.0F, 0.0F}, flux::Vec3f{1.0F, 0.0F, 0.0F},
            flux::Vec3f{0.0F, 1.0F, 0.0F}
        ),
        .triangles = sharedBuffer(flux::Vec3u{0, 1, 2}),
    };
}

[[nodiscard]] flux::Ref<flux::Primitive> makePrimitive(
    flux::Context &context, flux::Ref<flux::TriangleMesh const> const &mesh,
    flux::Ref<flux::EDF const> const &edf = {}
) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    if (edf)
        properties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    return context.create<flux::Primitive>(properties);
}

[[nodiscard]] flux::Ref<flux::EDF> makeEDF(flux::Context &context, flux::Spectrum const &radiance) {
    kira::Properties properties;
    properties.set("radiance", radiance);
    return context.create<flux::ConstantEDF>(properties);
}

[[nodiscard]] std::array<float, 12> scaled(float s) {
    return {
        s, 0.0F, 0.0F, 0.0F, 0.0F, s, 0.0F, 0.0F, 0.0F, 0.0F, s, 0.0F,
    };
}

void makePointLight(flux::Context &context) {
    kira::Properties properties;
    properties.set("position", flux::Vec3f{0.0F, 0.0F, 1.0F});
    properties.set("intensity", flux::Spectrum{1.0F, 1.0F, 1.0F});
    (void)context.create<flux::PointLight>(properties);
}

[[nodiscard]] float getPmf(flux::LightTableData const &table, std::uint32_t slot) {
    auto const &cdf = table.slots.cdf;
    auto const distribution = flux::LightPowerDistribution{
        .cdf = cdf.data(),
        .sum = cdf.empty() ? 0.0F : cdf.back(),
        .numLights = static_cast<std::uint32_t>(table.slots.handles.size()),
    };
    return distribution.pmf(slot);
}

/// Checks that the slot maps and handles name each other for every primitive.
void expectConsistentSlots(flux::SceneTableData const &scene, flux::LightTableData const &table) {
    ASSERT_EQ(table.slots.primSlots.size(), scene.primitives.size());
    ASSERT_EQ(table.lights.primAreaScales.size(), scene.primitives.size());

    // A primitive with a slot has an EDF, and its handle names it.
    for (std::uint32_t index = 0; index < scene.primitives.size(); ++index) {
        auto const slot = table.slots.primSlots[index];
        if (scene.primitives[index].isHole())
            EXPECT_EQ(slot, flux::LightTableData::invalidSlot);
        if (slot == flux::LightTableData::invalidSlot)
            continue;
        EXPECT_TRUE(scene.primitives[index].hasEDF());
        ASSERT_LT(slot, table.slots.handles.size());
        EXPECT_EQ(
            table.slots.handles[slot],
            (flux::LightHandle{.type = flux::LightType::Primitive, .index = index})
        );
    }

    // Every handle maps back to its slot.
    for (std::uint32_t slot = 0; slot < table.slots.handles.size(); ++slot) {
        auto const handle = table.slots.handles[slot];
        if (handle.type == flux::LightType::Primitive)
            EXPECT_EQ(table.slots.primSlots[handle.index], slot);
        if (handle.type == flux::LightType::EnvMap)
            EXPECT_EQ(table.slots.envMapSlot, slot);
    }
}
} // namespace

TEST(LightTableTests, BuildsEmptyTablesFromAnEmptyContext) {
    auto const context = flux::Context::create();
    auto scene = flux::SceneTableData{};
    scene.build(*context);

    auto table = flux::LightTableData{};
    table.build(scene, *context, std::nullopt);

    EXPECT_TRUE(table.lights.points.empty());
    EXPECT_TRUE(table.lights.primAreaScales.empty());
    EXPECT_TRUE(table.slots.handles.empty());
    EXPECT_TRUE(table.slots.cdf.empty());
    EXPECT_TRUE(table.slots.primSlots.empty());
    EXPECT_EQ(table.slots.envMapSlot, flux::LightTableData::invalidSlot);
}

TEST(LightTableTests, IndexesEmittersByPrimitiveIndexAroundAHole) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const edf = makeEDF(*context, {1.0F, 1.0F, 1.0F});
    (void)makePrimitive(*context, mesh, edf);
    makePrimitive(*context, mesh)->setTransform(scaled(3.0F));
    makePrimitive(*context, mesh, edf)->setVisible(false);
    makePrimitive(*context, mesh, edf)->setTransform(scaled(2.0F));

    auto scene = flux::SceneTableData{};
    scene.build(*context);
    auto table = flux::LightTableData{};
    table.build(scene, *context, 1.0F);

    // The hidden emitter at index 2 is a hole and gets no slot.
    ASSERT_EQ(scene.primitives.size(), 4);
    ASSERT_TRUE(scene.primitives[2].isHole());
    expectConsistentSlots(scene, table);
    EXPECT_NE(table.slots.primSlots[0], flux::LightTableData::invalidSlot);
    EXPECT_EQ(table.slots.primSlots[1], flux::LightTableData::invalidSlot);
    EXPECT_EQ(table.slots.primSlots[2], flux::LightTableData::invalidSlot);
    EXPECT_NE(table.slots.primSlots[3], flux::LightTableData::invalidSlot);

    // The emitter after the hole is selected by its own index.
    EXPECT_GT(getPmf(table, table.slots.primSlots[3]), 0.0F);

    // The environment map ranks before every emitter.
    EXPECT_EQ(table.slots.envMapSlot, 0);

    EXPECT_FLOAT_EQ(table.lights.primAreaScales[0], 1.0F);
    EXPECT_FLOAT_EQ(table.lights.primAreaScales[1], 9.0F);
    EXPECT_FLOAT_EQ(table.lights.primAreaScales[2], 0.0F);
    EXPECT_FLOAT_EQ(table.lights.primAreaScales[3], 4.0F);
}

TEST(LightTableTests, RebuildingMatchesABuildFromScratch) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const edf = makeEDF(*context, {1.0F, 1.0F, 1.0F});
    (void)makePrimitive(*context, mesh, edf);
    auto const hidden = makePrimitive(*context, mesh, edf);
    (void)makePrimitive(*context, mesh, edf);
    makePointLight(*context);

    auto scene = flux::SceneTableData{};
    scene.build(*context);
    auto table = flux::LightTableData{};
    table.build(scene, *context, 1.0F);

    hidden->setVisible(false);
    scene.build(*context);
    table.build(scene, *context, std::nullopt);

    auto freshScene = flux::SceneTableData{};
    freshScene.build(*context);
    auto fresh = flux::LightTableData{};
    fresh.build(freshScene, *context, std::nullopt);

    expectConsistentSlots(scene, table);
    EXPECT_EQ(table.lights.points.size(), fresh.lights.points.size());
    EXPECT_EQ(table.lights.primAreaScales, fresh.lights.primAreaScales);
    EXPECT_EQ(table.slots.handles, fresh.slots.handles);
    EXPECT_EQ(table.slots.cdf, fresh.slots.cdf);
    EXPECT_EQ(table.slots.primSlots, fresh.slots.primSlots);
    EXPECT_EQ(table.slots.envMapSlot, flux::LightTableData::invalidSlot);
}

TEST(LightTableTests, KeepsAZeroPowerEmitterWithZeroProbability) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    (void)makePrimitive(*context, mesh, makeEDF(*context, {0.0F, 0.0F, 0.0F}));
    makePointLight(*context);

    auto scene = flux::SceneTableData{};
    scene.build(*context);
    auto table = flux::LightTableData{};
    table.build(scene, *context, std::nullopt);

    auto const slot = table.slots.primSlots[0];
    ASSERT_NE(slot, flux::LightTableData::invalidSlot);
    EXPECT_FLOAT_EQ(getPmf(table, slot), 0.0F);
}

TEST(LightTableTests, DropsEmittersBeforeOtherLightsAtTheCap) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const edf = makeEDF(*context, {1.0F, 1.0F, 1.0F});
    (void)makePrimitive(*context, mesh, edf);
    makePointLight(*context);
    (void)makePrimitive(*context, mesh, edf);
    (void)makePrimitive(*context, mesh, edf);

    auto scene = flux::SceneTableData{};
    scene.build(*context);
    auto table = flux::LightTableData{};
    table.build(scene, *context, 1.0F, 3);

    // The point light and the environment map keep their slots; one emitter fits.
    expectConsistentSlots(scene, table);
    ASSERT_EQ(table.slots.handles.size(), 3);
    EXPECT_EQ(
        table.slots.handles[0], (flux::LightHandle{.type = flux::LightType::Point, .index = 0})
    );
    EXPECT_EQ(table.slots.envMapSlot, 1);
    EXPECT_EQ(table.slots.primSlots[0], 2);
    EXPECT_EQ(table.slots.primSlots[1], flux::LightTableData::invalidSlot);
    EXPECT_EQ(table.slots.primSlots[2], flux::LightTableData::invalidSlot);
    for (std::uint32_t slot = 0; slot < 3; ++slot)
        EXPECT_GT(getPmf(table, slot), 0.0F);
}

TEST(LightTableTests, DropsPointLightsPastTheCap) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    (void)makePrimitive(*context, mesh, makeEDF(*context, {1.0F, 1.0F, 1.0F}));
    makePointLight(*context);
    makePointLight(*context);

    auto scene = flux::SceneTableData{};
    scene.build(*context);
    auto table = flux::LightTableData{};
    table.build(scene, *context, 1.0F, 1);

    // Every point light keeps its identity, but only the first gets a slot.
    expectConsistentSlots(scene, table);
    EXPECT_EQ(table.lights.points.size(), 2);
    ASSERT_EQ(table.slots.handles.size(), 1);
    EXPECT_EQ(
        table.slots.handles[0], (flux::LightHandle{.type = flux::LightType::Point, .index = 0})
    );
    EXPECT_EQ(table.slots.envMapSlot, flux::LightTableData::invalidSlot);
    EXPECT_EQ(table.slots.primSlots[0], flux::LightTableData::invalidSlot);
}

TEST(LightTableTests, AssignsNoSlotWithAZeroCap) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    (void)makePrimitive(*context, mesh, makeEDF(*context, {1.0F, 1.0F, 1.0F}));
    makePointLight(*context);

    auto scene = flux::SceneTableData{};
    scene.build(*context);
    auto table = flux::LightTableData{};
    table.build(scene, *context, 1.0F, 0);

    EXPECT_TRUE(table.slots.handles.empty());
    EXPECT_TRUE(table.slots.cdf.empty());
    EXPECT_EQ(table.slots.envMapSlot, flux::LightTableData::invalidSlot);
    EXPECT_EQ(table.slots.primSlots[0], flux::LightTableData::invalidSlot);
    EXPECT_EQ(getPmf(table, 0), 0.0F);
}
