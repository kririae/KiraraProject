#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "TestUtils.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/RenderProduct.h"
#include "flux/Scene/TriangleMesh.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

namespace {
class ThrowingGapObject final : public flux::RenderObject {
    friend class flux::TXContext;

    explicit ThrowingGapObject(flux::TXContext &tx, kira::Properties properties)
        : RenderObject(tx, std::move(properties)) {
        throw std::runtime_error("intentional context ID gap");
    }
};

[[nodiscard]] kira::Properties primitiveProperties(flux::TriangleMesh const &mesh) {
    kira::Properties properties;
    properties.set("geometry_ctx_id", static_cast<std::int64_t>(mesh.getContextId()));
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
    auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));

    EXPECT_THROW((void)context->create<ThrowingGapObject>(), std::runtime_error);
    auto firstPrimitive = context->create<flux::Primitive>(primitiveProperties(*mesh));
    EXPECT_GT(firstPrimitive->getContextId(), mesh->getContextId() + 1);

    kira::Properties cameraProperties;
    cameraProperties.set("position", flux::Vec3f{0.25F, 0.25F, 1.0F});
    cameraProperties.set("look_at", flux::Vec3f{0.25F, 0.25F, 0.0F});
    auto camera = context->create<flux::Camera>(std::move(cameraProperties));
    kira::Properties productProperties;
    productProperties.set("width", std::uint32_t{3});
    productProperties.set("height", std::uint32_t{1});
    auto product = context->create<flux::RenderProduct>(std::move(productProperties));

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));
    EXPECT_NO_THROW(handler.render(*camera, *product, 0));

    EXPECT_THROW((void)context->create<ThrowingGapObject>(), std::runtime_error);
    auto secondPrimitive = context->create<flux::Primitive>(primitiveProperties(*mesh));
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

    EXPECT_NO_THROW(handler.render(*camera, *product, 1));
    handler.sync();
    EXPECT_NO_THROW(handler.render(*camera, *product, 1));
}
