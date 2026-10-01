#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/SceneTableData.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"

namespace {
using Transform = std::array<float, 12>;

constexpr Transform identity{
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
};

[[nodiscard]] Transform translated(float x, float y, float z) {
    return {
        1.0F, 0.0F, 0.0F, x, 0.0F, 1.0F, 0.0F, y, 0.0F, 0.0F, 1.0F, z,
    };
}

[[nodiscard]] flux::TriangleMesh::Data triangle() {
    return {
        .vertices =
            {flux::Vec3f{0.0F, 0.0F, 0.0F}, flux::Vec3f{1.0F, 0.0F, 0.0F},
             flux::Vec3f{0.0F, 1.0F, 0.0F}},
        .triangles = {flux::Vec3u{0, 1, 2}},
    };
}

[[nodiscard]] flux::Ref<flux::Primitive>
makePrimitive(flux::Context &context, flux::Ref<flux::TriangleMesh const> const &mesh) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    return context.create<flux::Primitive>(properties);
}

/// Compares the tables two builds produce, ignoring the geometry implementations
/// that only a backend builds.
void expectSameTables(flux::SceneTableData const &actual, flux::SceneTableData const &expected) {
    ASSERT_EQ(actual.primitives.size(), expected.primitives.size());
    for (std::size_t index = 0; index < actual.primitives.size(); ++index) {
        EXPECT_EQ(actual.primitives[index].geometryIndex, expected.primitives[index].geometryIndex);
        EXPECT_EQ(actual.primitives[index].bsdfIndex, expected.primitives[index].bsdfIndex);
        EXPECT_EQ(actual.primitives[index].edfIndex, expected.primitives[index].edfIndex);
    }
    EXPECT_EQ(actual.objects.primitives.size(), expected.objects.primitives.size());
    EXPECT_EQ(actual.transforms, expected.transforms);
    EXPECT_EQ(actual.objects.meshes.size(), expected.objects.meshes.size());
    EXPECT_EQ(actual.bsdfs.size(), expected.bsdfs.size());
    EXPECT_EQ(actual.edfs.size(), expected.edfs.size());
}
} // namespace

TEST(SceneTableTests, BuildsEmptyTablesFromAnEmptyContext) {
    auto const context = flux::Context::create();

    auto table = flux::SceneTableData{};
    table.build(*context);

    EXPECT_TRUE(table.objects.primitives.empty());
    EXPECT_TRUE(table.primitives.empty());
    EXPECT_TRUE(table.transforms.empty());
    EXPECT_TRUE(table.objects.meshes.empty());
    EXPECT_TRUE(table.bsdfs.empty());
    EXPECT_TRUE(table.edfs.empty());
}

TEST(SceneTableTests, SkipsInvisiblePrimitivesAndKeepsTheDenseOrder) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const first = makePrimitive(*context, mesh);
    auto const hidden = makePrimitive(*context, mesh);
    auto const third = makePrimitive(*context, mesh);
    first->setTransform(identity);
    third->setTransform(translated(1.0F, 2.0F, 3.0F));
    hidden->setVisible(false);

    auto table = flux::SceneTableData{};
    table.build(*context);

    ASSERT_EQ(table.primitives.size(), 2);
    ASSERT_EQ(table.objects.primitives.size(), 2);
    EXPECT_EQ(table.objects.primitives[0], first);
    EXPECT_EQ(table.objects.primitives[1], third);
    EXPECT_EQ(table.transforms[0], identity);
    EXPECT_EQ(table.transforms[1], translated(1.0F, 2.0F, 3.0F));
}

TEST(SceneTableTests, GivesOneGeometryIndexToEveryPrimitiveThatSharesAMesh) {
    auto context = flux::Context::create();
    auto const firstMesh = context->create<flux::TriangleMesh>(triangle());
    auto const secondMesh = context->create<flux::TriangleMesh>(triangle());
    (void)makePrimitive(*context, firstMesh);
    (void)makePrimitive(*context, secondMesh);
    (void)makePrimitive(*context, firstMesh);

    auto table = flux::SceneTableData{};
    table.build(*context);

    ASSERT_EQ(table.objects.meshes.size(), 2);
    EXPECT_EQ(table.objects.meshes[0], firstMesh);
    EXPECT_EQ(table.objects.meshes[1], secondMesh);

    ASSERT_EQ(table.primitives.size(), 3);
    EXPECT_EQ(table.primitives[0].geometryIndex, 0);
    EXPECT_EQ(table.primitives[1].geometryIndex, 1);
    EXPECT_EQ(table.primitives[2].geometryIndex, 0);
}

TEST(SceneTableTests, ExcludesAMeshThatNoVisiblePrimitiveUses) {
    auto context = flux::Context::create();
    auto const visibleMesh = context->create<flux::TriangleMesh>(triangle());
    auto const hiddenMesh = context->create<flux::TriangleMesh>(triangle());
    (void)makePrimitive(*context, visibleMesh);
    auto const hidden = makePrimitive(*context, hiddenMesh);
    hidden->setVisible(false);

    auto table = flux::SceneTableData{};
    table.build(*context);

    ASSERT_EQ(table.objects.meshes.size(), 1);
    EXPECT_EQ(table.objects.meshes[0], visibleMesh);
}

TEST(SceneTableTests, CopiesEveryBsdfAndEdfTheContextHolds) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const unusedBsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    (void)unusedBsdf;

    auto table = flux::SceneTableData{};
    table.build(*context);

    ASSERT_EQ(table.bsdfs.size(), context->getBSDFIndexLimit());
    ASSERT_FALSE(table.bsdfs.empty());
    for (auto const &bsdf : table.bsdfs)
        EXPECT_NE(bsdf.type, flux::BSDFType::Count);
}

TEST(SceneTableTests, ResolvesTheBsdfAndEdfIndicesOfAPrimitive) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto const edf = context->create<flux::ConstantEDF>(kira::Properties{});

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    properties.set("edf_ctx_id", static_cast<std::int64_t>(edf->getContextId()));
    (void)context->create<flux::Primitive>(properties);
    (void)makePrimitive(*context, mesh);

    auto table = flux::SceneTableData{};
    table.build(*context);

    ASSERT_EQ(table.primitives.size(), 2);
    EXPECT_EQ(table.primitives[0].getBSDFIndex(), context->getBSDFIndex(bsdf->getContextId()));
    EXPECT_EQ(table.primitives[0].getEDFIndex(), context->getEDFIndex(edf->getContextId()));
    EXPECT_FALSE(table.primitives[1].hasBSDF());
    EXPECT_FALSE(table.primitives[1].hasEDF());
}

TEST(SceneTableTests, RebuildingMatchesABuildFromScratch) {
    auto context = flux::Context::create();
    auto const mesh = context->create<flux::TriangleMesh>(triangle());
    auto const otherMesh = context->create<flux::TriangleMesh>(triangle());
    auto const first = makePrimitive(*context, mesh);
    auto const second = makePrimitive(*context, mesh);
    auto const third = makePrimitive(*context, otherMesh);

    auto table = flux::SceneTableData{};
    table.build(*context);

    second->setVisible(false);
    third->setTransform(translated(4.0F, 5.0F, 6.0F));
    table.build(*context);

    auto freshContext = flux::Context::create();
    auto const freshMesh = freshContext->create<flux::TriangleMesh>(triangle());
    auto const freshOtherMesh = freshContext->create<flux::TriangleMesh>(triangle());
    makePrimitive(*freshContext, freshMesh)->setTransform(first->getTransform());
    auto const freshThird = makePrimitive(*freshContext, freshOtherMesh);
    freshThird->setTransform(translated(4.0F, 5.0F, 6.0F));

    auto freshTable = flux::SceneTableData{};
    freshTable.build(*freshContext);

    expectSameTables(table, freshTable);
}
