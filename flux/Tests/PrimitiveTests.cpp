#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

#include "flux/Scene/Context.h"
#include "flux/Scene/Geometry.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "kira/Anyhow.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

namespace {
[[nodiscard]] kira::Properties triangleProperties() {
    kira::Properties properties;
    properties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    return properties;
}
} // namespace

TEST(PrimitiveTests, ResolvesContextReferencesDuringConstruction) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    auto primitive = context->create<flux::Primitive>(properties);

    EXPECT_EQ(primitive->getGeometry(), mesh);
    EXPECT_EQ(primitive->getGeometry()->getType(), flux::GeometryType::TriangleMesh);
    EXPECT_EQ(primitive->getBSDF(), bsdf);
}

TEST(PrimitiveTests, CreatesFromGeometry) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto primitive = context->create<flux::Primitive>(mesh);

    EXPECT_EQ(primitive->getGeometry(), mesh);
}

TEST(PrimitiveTests, ReadsRowMajorTransform) {
    auto context = flux::Context::create();
    context->getFileResolver().prepend(FLUX_TEST_FIXTURES_DIR);
    auto props = kira::Properties::parse(
        R"(
        type = "trimesh"
        path = "Triangle.obj"
        transform = [0, 0, 4, 10, 0, 4, 0, 20, -4, 0, 0, 30]
    )",
        "Transform.toml"
    );
    auto primitive = context->create<flux::Primitive>(props);
    EXPECT_EQ(
        primitive->getTransform(), (std::array<float, 12>{0, 0, 4, 10, 0, 4, 0, 20, -4, 0, 0, 30})
    );
    EXPECT_TRUE(props.is_all_used());
}

TEST(PrimitiveTests, RejectsReferencesOfTheWrongKind) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});

    kira::Properties geometryIsBsdf;
    geometryIsBsdf.set("geometry_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    EXPECT_THROW((void)context->create<flux::Primitive>(geometryIsBsdf), kira::Anyhow);

    kira::Properties bsdfIsGeometry;
    bsdfIsGeometry.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    bsdfIsGeometry.set("bsdf_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    EXPECT_THROW((void)context->create<flux::Primitive>(bsdfIsGeometry), kira::Anyhow);
}

TEST(PrimitiveTests, CreatesInlineGeometryAndBsdf) {
    auto context = flux::Context::create();
    auto properties = triangleProperties();
    properties.set("type", "trimesh");

    kira::Properties bsdfProperties;
    bsdfProperties.set("type", "diffuse");
    properties.set("bsdf", bsdfProperties);
    auto primitive = context->create<flux::Primitive>(properties);

    EXPECT_EQ(primitive->getGeometry()->getType(), flux::GeometryType::TriangleMesh);
    ASSERT_NE(primitive->getBSDF(), nullptr);
    EXPECT_EQ(context->getNumContextObjects(), 4);
}

TEST(PrimitiveTests, SettersRebindRelationships) {
    auto context = flux::Context::create();
    auto geometry = context->create<flux::TriangleMesh>(triangleProperties());
    auto replacement = context->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto edf = context->create<flux::ConstantEDF>(kira::Properties{});

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(geometry->getContextId()));
    auto primitive = context->create<flux::Primitive>(properties);

    primitive->setGeometry(replacement);
    EXPECT_EQ(primitive->getGeometry(), replacement);
    EXPECT_THROW(primitive->setGeometry(nullptr), std::invalid_argument);
    EXPECT_EQ(primitive->getGeometry(), replacement);

    primitive->setBSDF(bsdf);
    EXPECT_EQ(primitive->getBSDF(), bsdf);
    EXPECT_EQ(primitive->getBSDF(), bsdf);

    primitive->setBSDF(nullptr);
    EXPECT_EQ(primitive->getBSDF(), nullptr);

    primitive->setEDF(edf);
    EXPECT_EQ(primitive->getEDF(), edf);
    EXPECT_EQ(primitive->getEDF(), edf);
}

TEST(PrimitiveTests, BuildsItsTableEntryFromContextIndices) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>(kira::Properties{});
    auto edf = context->create<flux::EDF>(kira::Properties{});
    auto const geometryIndex = context->getIndex<flux::Geometry>(mesh->getContextId());
    auto const bsdfIndex = context->getIndex<flux::BSDF>(bsdf->getContextId());
    auto const edfIndex = context->getIndex<flux::EDF>(edf->getContextId());

    kira::Properties bare;
    bare.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    auto plain = context->create<flux::Primitive>(bare);
    auto full = context->create<flux::Primitive>(bare);
    full->setBSDF(bsdf);
    full->setEDF(edf);

    // A visible primitive resolves the indices it binds.
    auto const plainImpl = plain->getImpl();
    EXPECT_FALSE(plainImpl.isHole());
    EXPECT_EQ(plainImpl.getGeometryIndex(), geometryIndex);
    EXPECT_FALSE(plainImpl.hasBSDF());
    EXPECT_FALSE(plainImpl.hasEDF());
    auto const fullImpl = full->getImpl();
    EXPECT_EQ(fullImpl.getGeometryIndex(), geometryIndex);
    EXPECT_EQ(fullImpl.getBSDFIndex(), bsdfIndex);
    EXPECT_EQ(fullImpl.getEDFIndex(), edfIndex);

    // A hidden primitive is a hole, and only a visible primitive with an EDF is a light.
    EXPECT_FALSE(plain->isLight());
    EXPECT_TRUE(full->isLight());
    full->setVisible(false);
    EXPECT_TRUE(full->getImpl().isHole());
    EXPECT_FALSE(full->isLight());
}
