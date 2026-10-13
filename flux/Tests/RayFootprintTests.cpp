#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

#include "flux/Core/RayFootprint.h"

TEST(RayFootprintTests, PropagatesAndPacksRayFootprints) {
    auto footprint = flux::RayFootprint{
        .cones = {
            flux::RayCone{.angle = 0.1F, .width = 0.2F},
            flux::RayCone{.angle = -0.1F, .width = 0.05F},
        },
    };
    footprint.propagate(1.0F);

    EXPECT_FLOAT_EQ(footprint.cones[0].angle, 0.1F);
    EXPECT_FLOAT_EQ(footprint.cones[0].width, 0.3F);
    EXPECT_FLOAT_EQ(footprint.cones[1].angle, 0.1F);
    EXPECT_FLOAT_EQ(footprint.cones[1].width, 0.05F);

    auto const unpacked = flux::PackedRayFootprint::pack(footprint).unpack();
    EXPECT_NEAR(unpacked.cones[0].angle, footprint.cones[0].angle, 1.0e-3F);
    EXPECT_NEAR(unpacked.cones[0].width, footprint.cones[0].width, 2.0e-3F);
    EXPECT_NEAR(unpacked.cones[1].angle, footprint.cones[1].angle, 1.0e-3F);
    EXPECT_NEAR(unpacked.cones[1].width, footprint.cones[1].width, 1.0e-3F);
}

TEST(RayFootprintTests, RefractsRayCones) {
    auto refracted = flux::RayCone{.angle = 0.1F, .width = 0.2F};
    refracted.refract(2.0F);
    EXPECT_FLOAT_EQ(refracted.angle, 0.05F);
}

TEST(RayFootprintTests, ExpandsRayConesFromBsdfValue) {
    auto cone = flux::RayCone{.angle = 0.0F, .width = -0.2F};
    cone.scatter(std::numbers::inv_pi_v<float>);

    EXPECT_FLOAT_EQ(cone.angle, 0.25F);
    EXPECT_FLOAT_EQ(cone.width, 0.2F);
}

TEST(RayFootprintTests, BroadensConvergingRayCones) {
    auto cone = flux::RayCone{.angle = -0.1F, .width = 0.2F};
    cone.scatter(2.0F * std::numbers::inv_pi_v<float>);

    EXPECT_GT(cone.angle, -0.1F);
    EXPECT_LT(cone.angle, 0.25F);
    EXPECT_FLOAT_EQ(cone.width, 0.2F);
}

TEST(RayFootprintTests, ProjectsOrthogonalFootprintAxes) {
    // A normal along z and one along x take the two tangent choices at normal incidence.
    auto const frames = std::array{
        std::pair{flux::Vec3f{0.0F, 0.0F, 1.0F}, flux::Vec3f{1.0F, 0.0F, 0.0F}},
        std::pair{flux::Vec3f{1.0F, 0.0F, 0.0F}, flux::Vec3f{0.0F, 1.0F, 0.0F}},
    };
    for (auto const &[normal, tangent] : frames) {
        for (auto const width : {0.0F, 0.2F}) {
            auto const footprint = flux::RayFootprint{
                .cones = {flux::RayCone{.width = width}, flux::RayCone{}},
            };
            for (auto const cosine : {1.0F, 0.5F, 0.01F, 0.0F}) {
                auto const direction =
                    tangent * std::sqrt(1.0F - cosine * cosine) + normal * cosine;
                flux::Vec3f dx{}, dy{};
                footprint.project(direction, normal, dx, dy);
                EXPECT_NEAR(dx.norm(), width / std::max(cosine, 1.0F / 16.0F), 1.0e-6F);
                EXPECT_NEAR(dy.norm(), width, 1.0e-6F);
                EXPECT_NEAR(dx.dot(dy), 0.0F, 1.0e-6F);
                EXPECT_NEAR(dx.dot(normal), 0.0F, 1.0e-6F);
                EXPECT_NEAR(dy.dot(normal), 0.0F, 1.0e-6F);
            }
        }
    }
}
