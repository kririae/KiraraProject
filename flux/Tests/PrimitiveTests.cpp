#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
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

TEST(PrimitiveTests, LinksGeometryAndStoresInstanceProperties) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());
    auto bsdf = context->create<flux::DiffuseBSDF>();

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    auto primitive = context->create<flux::Primitive>(std::move(properties));
    std::array const transform{
        1.0F, 0.0F, 0.0F, 2.0F, 0.0F, 1.0F, 0.0F, 3.0F, 0.0F, 0.0F, 1.0F, 4.0F,
    };
    primitive->setTransform(transform);
    primitive->setVisible(false);

    context->commit();

    EXPECT_EQ(primitive->getGeometry(), mesh);
    EXPECT_EQ(primitive->getBSDF(), bsdf);
    EXPECT_EQ(primitive->getTransform(), transform);
    EXPECT_FALSE(primitive->isVisible());
}

TEST(PrimitiveTests, AllowsAPrimitiveWithoutABsdf) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());

    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    auto primitive = context->create<flux::Primitive>(std::move(properties));

    EXPECT_FALSE(primitive->getBSDFContextId().has_value());
    EXPECT_EQ(primitive->getBSDF(), nullptr);
}

TEST(PrimitiveTests, RejectsInvalidGeometryIds) {
    auto context = flux::Context::create();

    kira::Properties negative;
    negative.set("geometry_ctx_id", std::int64_t{-1});
    EXPECT_THROW((void)context->create<flux::Primitive>(std::move(negative)), kira::Anyhow);

    kira::Properties unknown;
    unknown.set("geometry_ctx_id", std::int64_t{42});
    (void)context->create<flux::Primitive>(std::move(unknown));
    EXPECT_THROW(context->commit(), std::out_of_range);
}

TEST(PrimitiveTests, RejectsInvalidBsdfIds) {
    auto context = flux::Context::create();
    auto mesh = context->create<flux::TriangleMesh>(triangleProperties());

    kira::Properties negative;
    negative.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    negative.set("bsdf_ctx_id", std::int64_t{-1});
    EXPECT_THROW((void)context->create<flux::Primitive>(std::move(negative)), kira::Anyhow);

    kira::Properties wrongType;
    wrongType.set("geometry_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    wrongType.set("bsdf_ctx_id", static_cast<std::int64_t>(mesh->getContextId()));
    (void)context->create<flux::Primitive>(std::move(wrongType));
    EXPECT_THROW(context->commit(), kira::Anyhow);
}
