#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "TestUtils.h"
#include "flux/Core/Ray.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"
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
    kira::Properties meshProperties;
    meshProperties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");
    auto mesh = context->create<flux::TriangleMesh>(std::move(meshProperties));

    EXPECT_THROW((void)context->create<ThrowingGapObject>(), std::runtime_error);
    auto firstPrimitive = context->create<flux::Primitive>(primitiveProperties(*mesh));
    EXPECT_GT(firstPrimitive->getContextId(), mesh->getContextId() + 1);

    flux::OptixHandler handler(context, std::filesystem::path(FLUX_TEST_OPTIX_IR));

    std::array const rays{
        flux::Ray{
            .origin = flux::Vec3f{0.25F, 0.25F, 1.0F},
            .direction = flux::Vec3f{0.0F, 0.0F, -1.0F},
        },
        flux::Ray{
            .origin = flux::Vec3f{1.25F, 1.25F, 1.0F},
            .direction = flux::Vec3f{0.0F, 0.0F, -1.0F},
        },
        flux::Ray{
            .origin = flux::Vec3f{2.25F, 0.25F, 1.0F},
            .direction = flux::Vec3f{0.0F, 0.0F, -1.0F},
        },
    };

    auto const initialHits = handler.intersect(rays);

    ASSERT_EQ(initialHits.size(), rays.size());
    EXPECT_TRUE(initialHits[0].isHit());
    EXPECT_FLOAT_EQ(initialHits[0].distance, 1.0F);
    EXPECT_EQ(initialHits[0].triangleIndex, 0);
    EXPECT_EQ(initialHits[0].instanceIndex, 0);
    EXPECT_EQ(initialHits[0].geometryIndex, 0);
    EXPECT_FALSE(initialHits[1].isHit());
    EXPECT_FALSE(initialHits[2].isHit());

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

    auto const staleHits = handler.intersect(rays);
    ASSERT_EQ(staleHits.size(), rays.size());
    EXPECT_FALSE(staleHits[2].isHit());

    handler.sync();

    auto const updatedHits = handler.intersect(rays);

    ASSERT_EQ(updatedHits.size(), rays.size());
    EXPECT_TRUE(updatedHits[0].isHit());
    EXPECT_EQ(updatedHits[0].instanceIndex, 0);
    EXPECT_EQ(updatedHits[0].geometryIndex, 0);
    EXPECT_FALSE(updatedHits[1].isHit());
    EXPECT_TRUE(updatedHits[2].isHit());
    EXPECT_FLOAT_EQ(updatedHits[2].distance, 1.0F);
    EXPECT_EQ(updatedHits[2].triangleIndex, 0);
    EXPECT_EQ(updatedHits[2].instanceIndex, 1);
    EXPECT_EQ(updatedHits[2].geometryIndex, 0);
}
