#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <utility>

#include "TestUtils.h"
#include "flux/Core/Ray.h"
#include "flux/Geometry/TriangleMesh.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Scene/Context.h"

#ifndef FLUX_TEST_FIXTURES_DIR
#error "FLUX_TEST_FIXTURES_DIR must name the Flux test fixtures directory"
#endif

#ifndef FLUX_TEST_OPTIX_IR
#error "FLUX_TEST_OPTIX_IR must name the test OptiX IR module"
#endif

TEST(OptixGeometryTests, IntersectsTriangleMeshThroughLaunch) {
    if (!flux::test::hasCudaMemoryPoolSupport())
        GTEST_SKIP() << "Stream-ordered CUDA allocation is unavailable";

    kira::Properties properties;
    properties.set("path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "Triangle.obj");

    auto context = flux::Context::create();
    (void)context->create<flux::TriangleMesh>(std::move(properties));

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
    EXPECT_EQ(initialHits[0].primitiveIndex, 0);
    EXPECT_EQ(initialHits[0].geometryIndex, 0);
    EXPECT_FALSE(initialHits[1].isHit());
    EXPECT_FALSE(initialHits[2].isHit());

    kira::Properties offsetProperties;
    offsetProperties.set(
        "path", std::filesystem::path(FLUX_TEST_FIXTURES_DIR) / "OffsetTriangle.obj"
    );
    (void)context->create<flux::TriangleMesh>(std::move(offsetProperties));

    auto const staleHits = handler.intersect(rays);
    ASSERT_EQ(staleHits.size(), rays.size());
    EXPECT_FALSE(staleHits[2].isHit());

    handler.sync();

    auto const updatedHits = handler.intersect(rays);

    ASSERT_EQ(updatedHits.size(), rays.size());
    EXPECT_TRUE(updatedHits[0].isHit());
    EXPECT_EQ(updatedHits[0].geometryIndex, 0);
    EXPECT_FALSE(updatedHits[1].isHit());
    EXPECT_TRUE(updatedHits[2].isHit());
    EXPECT_FLOAT_EQ(updatedHits[2].distance, 1.0F);
    EXPECT_EQ(updatedHits[2].primitiveIndex, 0);
    EXPECT_EQ(updatedHits[2].geometryIndex, 1);
}
