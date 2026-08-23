#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>

#include "flux/Scene/Context.h"
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
    auto bsdf = context->create<flux::DiffuseBSDF>();

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    auto primitive = context->create<flux::Primitive>(properties);
    std::array const transform{
        1.0F, 0.0F, 0.0F, 2.0F, 0.0F, 1.0F, 0.0F, 3.0F, 0.0F, 0.0F, 1.0F, 4.0F,
    };
    primitive->setTransform(transform);
    primitive->setVisible(false);

    EXPECT_EQ(primitive->getGeometry(), mesh);
    EXPECT_EQ(primitive->getGeometry()->getType(), flux::GeometryType::TriangleMesh);
    EXPECT_EQ(primitive->getBSDF(), bsdf);
    EXPECT_EQ(primitive->getTransform(), transform);
    EXPECT_FALSE(primitive->isVisible());
}

TEST(PrimitiveTests, AllowsAPrimitiveWithoutABsdf) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    auto primitive = context->create<flux::Primitive>(properties);

    EXPECT_EQ(primitive->getBSDF(), nullptr);
}

TEST(PrimitiveTests, RejectsInvalidGeometryIds) {
    auto context = flux::Context::create();

    kira::Properties negative;
    negative.set("geometry_ctx_id", std::int64_t{-1});
    EXPECT_THROW((void)context->create<flux::Primitive>(negative), std::out_of_range);

    kira::Properties unknown;
    unknown.set("geometry_ctx_id", std::int64_t{42});
    EXPECT_THROW((void)context->create<flux::Primitive>(unknown), std::out_of_range);
}

TEST(PrimitiveTests, RejectsNonGeometryReferences) {
    auto context = flux::Context::create();
    auto bsdf = context->create<flux::DiffuseBSDF>();

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    EXPECT_THROW((void)context->create<flux::Primitive>(properties), kira::Anyhow);
}

TEST(PrimitiveTests, RejectsInvalidBsdfIds) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());

    kira::Properties negative;
    negative.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    negative.set("bsdf_ctx_id", std::int64_t{-1});
    EXPECT_THROW((void)context->create<flux::Primitive>(negative), std::out_of_range);

    kira::Properties wrongType;
    wrongType.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    wrongType.set("bsdf_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    EXPECT_THROW((void)context->create<flux::Primitive>(wrongType), kira::Anyhow);
}

TEST(PrimitiveTests, RejectsUnresolvedBsdfNames) {
    auto context = flux::Context::create();
    auto properties = triangleProperties();
    properties.set("type", "trimesh");
    properties.set("bsdf", "grey");

    EXPECT_THROW((void)context->create<flux::Primitive>(properties), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), 0);
}

TEST(PrimitiveTests, ResolvesGeometryPathsThroughTheContext) {
    auto context = flux::Context::create();
    context->getFileResolver().prepend(FLUX_TEST_FIXTURES_DIR);
    kira::Properties props;
    props.set("path", "Triangle.obj");

    auto mesh = context->create<flux::TriangleMesh>(props);

    EXPECT_EQ(mesh->getTriangles().size(), 1);
}

TEST(PrimitiveTests, RejectsMissingGeometryPaths) {
    auto context = flux::Context::create();
    context->getFileResolver().prepend(FLUX_TEST_FIXTURES_DIR);
    kira::Properties props;
    props.set("path", "Missing.obj");

    EXPECT_THROW((void)context->create<flux::TriangleMesh>(props), kira::Anyhow);
    EXPECT_EQ(context->getNumContextObjects(), 0);
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

TEST(PrimitiveTests, SettersKeepRelationshipsInsideTheContext) {
    auto context = flux::Context::create();
    auto otherContext = flux::Context::create();
    auto geometry = context->create<flux::TriangleMesh>(triangleProperties());
    auto replacement = context->create<flux::TriangleMesh>(triangleProperties());
    auto foreignGeometry = otherContext->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>();
    auto foreignBsdf = otherContext->create<flux::DiffuseBSDF>();
    auto edf = context->create<flux::ConstantEDF>();
    auto foreignEdf = otherContext->create<flux::ConstantEDF>();

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(geometry->getContextId()));
    auto primitive = context->create<flux::Primitive>(properties);

    primitive->setGeometry(replacement);
    EXPECT_EQ(primitive->getGeometry(), replacement);
    EXPECT_THROW(primitive->setGeometry(nullptr), kira::Anyhow);
    EXPECT_THROW(primitive->setGeometry(foreignGeometry), kira::Anyhow);
    EXPECT_EQ(primitive->getGeometry(), replacement);

    primitive->setBSDF(bsdf);
    EXPECT_EQ(primitive->getBSDF(), bsdf);
    EXPECT_THROW(primitive->setBSDF(foreignBsdf), kira::Anyhow);
    EXPECT_EQ(primitive->getBSDF(), bsdf);

    primitive->setBSDF(nullptr);
    EXPECT_EQ(primitive->getBSDF(), nullptr);

    primitive->setEDF(edf);
    EXPECT_EQ(primitive->getEDF(), edf);
    EXPECT_THROW(primitive->setEDF(foreignEdf), kira::Anyhow);
    EXPECT_EQ(primitive->getEDF(), edf);
}
