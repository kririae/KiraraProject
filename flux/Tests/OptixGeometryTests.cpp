#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>

#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Light.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

namespace {
class ThrowingGapObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingGapObject(flux::TXContext &tx, kira::Properties const &) : RenderObject(tx) {
        throw std::runtime_error("intentional context ID gap");
    }
};

[[nodiscard]] kira::Properties
primitiveProperties(flux::TriangleMesh const &mesh, flux::BSDF const *bsdf = nullptr) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh.getContextId()));
    if (bsdf)
        properties.set("bsdf_ctx_id", static_cast<std::int64_t>(bsdf->getContextId()));
    return properties;
}
} // namespace

TEST(OptixGeometryTests, MaterializesSparseHostObjectsAsDenseInstances) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();
    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(meshProperties);
    kira::Properties bsdfProperties;
    bsdfProperties.set("R", flux::Spectrum{0.2F, 0.4F, 0.8F});
    auto bsdf = context->create<flux::DiffuseBSDF>(bsdfProperties);

    EXPECT_THROW((void)context->create<ThrowingGapObject>(), std::runtime_error);
    auto firstPrimitive = context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get()));
    EXPECT_GT(firstPrimitive->getContextId(), mesh->getContextId() + 1);

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    auto camera = flux::Camera::create(cameraProperties);
    kira::Properties productProperties;
    productProperties.set("resolution", flux::Vec2u{3, 1});
    auto product = flux::RenderProduct::create(camera, productProperties);

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.render(*product, 1));

    EXPECT_THROW((void)context->create<ThrowingGapObject>(), std::runtime_error);
    auto secondPrimitive = context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get()));
    secondPrimitive->setTransform({
        1.0F,
        0.0F,
        0.0F,
        2.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
    });
    EXPECT_GT(secondPrimitive->getContextId(), firstPrimitive->getContextId() + 1);

    EXPECT_NO_THROW(handler.render(*product, 1));
    handler.sync();
    EXPECT_EQ(handler.getAccumulatedSamples(*product), 0);
    EXPECT_NO_THROW(handler.render(*product, 1));
}

TEST(OptixGeometryTests, RendersDirectLightIntoColorChannel) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    auto context = flux::Context::create();
    (void)context->create<flux::PathIntegrator>();
    (void)context->create<flux::IndependentSampler>();

    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(meshProperties);
    auto bsdf = context->create<flux::DiffuseBSDF>();
    (void)context->create<flux::Primitive>(primitiveProperties(*mesh, bsdf.get()));

    kira::Properties lightProperties;
    lightProperties.set("position", flux::Vec3f{0.75F, 0.25F, 1.0F});
    lightProperties.set("intensity", flux::Spectrum{1.0F, 1.0F, 1.0F});
    (void)context->create<flux::PointLight>(lightProperties);

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    cameraProperties.set("fov", 1.0F);
    auto camera = flux::Camera::create(cameraProperties);
    kira::Properties productProperties;
    productProperties.set("resolution", flux::Vec2u{1, 1});
    auto product = flux::RenderProduct::create(camera, productProperties);
    product->getFilm().setChannels(flux::FilmChannels::Color);

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    handler.render(*product, 4);
    handler.download(*product);

    auto const color = product->getFilm().getChannel<flux::ColorChannel>();
    ASSERT_EQ(color.size(), 1);
    EXPECT_GT(color[0].x(), 0.0F);
    EXPECT_GT(color[0].y(), 0.0F);
    EXPECT_GT(color[0].z(), 0.0F);

    auto blocker = context->create<flux::Primitive>(primitiveProperties(*mesh));
    blocker->setTransform({
        0.0F,
        0.0F,
        1.0F,
        0.5F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
    });

    handler.sync();
    handler.render(*product, 4);
    handler.download(*product);
    auto const blockedColor = product->getFilm().getChannel<flux::ColorChannel>();
    EXPECT_EQ(blockedColor[0], flux::Spectrum{});
}
