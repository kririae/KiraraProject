#include <gtest/gtest.h>

#include "IPC/CCDPrimitives.h"

namespace {
using Vec3d = krd::Vector3d;
using Vec3f = krd::Vector3f;

constexpr double ToiTolerance = 1e-10;
constexpr double UnchangedToi = 123.0;
constexpr krd::ipc::CCDConfig LooseCCD{
    .rootToleranceScale = 4096,
    .degenerateToleranceScale = 2048,
    .barycentricToleranceScale = 128,
    .segmentDistanceToleranceScale = 8192,
};

static_assert(LooseCCD.valid());

Vec3d vd(double x, double y, double z) { return Vec3d{x, y, z}; }
Vec3f vf(float x, float y, float z) { return Vec3f{x, y, z}; }
} // namespace

TEST(CCDConfigTests, CustomConfigCanBeUsedByPublicPrimitives) {
    double pointTriangleToi = -1.0;
    EXPECT_TRUE((krd::ipc::CCDPointTriangle<float, double, LooseCCD>(
        vf(0.25F, 0.25F, 1.0F), vf(0.0F, 0.0F, -2.0F), //
        vf(0.0F, 0.0F, 0.0F), vf(0.0F, 0.0F, 0.0F),    //
        vf(1.0F, 0.0F, 0.0F), vf(0.0F, 0.0F, 0.0F),    //
        vf(0.0F, 1.0F, 0.0F), vf(0.0F, 0.0F, 0.0F),    //
        pointTriangleToi
    )));
    EXPECT_NEAR(pointTriangleToi, 0.5, ToiTolerance);

    double edgeEdgeToi = -1.0;
    EXPECT_TRUE((krd::ipc::CCDEdgeEdge<double, double, LooseCCD>(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),   //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),   //
        vd(-0.5, 0.5, 1.0), vd(0.0, 0.0, -2.0), //
        vd(0.5, 0.5, 1.0), vd(0.0, 0.0, -2.0),  //
        edgeEdgeToi                             //
    )));
    EXPECT_NEAR(edgeEdgeToi, 0.5, ToiTolerance);
}

TEST(CCDPointTriangleTests, InteriorHitWritesEarliestToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDPointTriangle(
        vd(0.25, 0.25, 1.0), vd(0.0, 0.0, -2.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),    //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),    //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),    //
        toi
    ));
    EXPECT_NEAR(toi, 0.5, ToiTolerance);
}

TEST(CCDPointTriangleTests, MissLeavesToiUnchanged) {
    double toi = UnchangedToi;
    EXPECT_FALSE(krd::ipc::CCDPointTriangle(
        vd(1.25, 1.25, 1.0), vd(0.0, 0.0, -2.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),    //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),    //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),    //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, UnchangedToi);
}

TEST(CCDPointTriangleTests, InitialEdgeContactWritesZeroToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDPointTriangle(
        vd(0.5, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, 0.0);
}

TEST(CCDPointTriangleTests, FinalVertexContactWritesOneToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDPointTriangle(
        vd(0.0, 0.0, 1.0), vd(0.0, 0.0, -1.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),  //
        toi
    ));
    EXPECT_NEAR(toi, 1.0, ToiTolerance);
}

TEST(CCDPointTriangleTests, ProjectionInsideWithoutCoplanarityMisses) {
    double toi = UnchangedToi;
    EXPECT_FALSE(krd::ipc::CCDPointTriangle(
        vd(0.25, 0.25, 1.0), vd(0.0, 0.0, 0.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),   //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),   //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),   //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, UnchangedToi);
}

TEST(CCDPointTriangleTests, DegenerateTriangleLeavesToiUnchanged) {
    double toi = UnchangedToi;
    EXPECT_FALSE(krd::ipc::CCDPointTriangle(
        vd(0.25, 0.0, 1.0), vd(0.0, 0.0, -2.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),   //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),   //
        vd(2.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),   //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, UnchangedToi);
}

TEST(CCDPointTriangleTests, CoplanarPointSlidesIntoTriangle) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDPointTriangle(
        vd(-0.5, 0.25, 0.0), vd(0.75, 0.0, 0.0), //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),    //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),    //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),    //
        toi
    ));
    EXPECT_NEAR(toi, 2.0 / 3.0, ToiTolerance);
}

TEST(CCDPointTriangleTests, FloatInputCanUseDoubleArithmetic) {
    double toi = -1.0;
    EXPECT_TRUE((krd::ipc::CCDPointTriangle<float, double>(
        vf(0.25F, 0.25F, 1.0F), vf(0.0F, 0.0F, -2.0F), //
        vf(0.0F, 0.0F, 0.0F), vf(0.0F, 0.0F, 0.0F),    //
        vf(1.0F, 0.0F, 0.0F), vf(0.0F, 0.0F, 0.0F),    //
        vf(0.0F, 1.0F, 0.0F), vf(0.0F, 0.0F, 0.0F),    //
        toi
    )));
    EXPECT_NEAR(toi, 0.5, ToiTolerance);
}

TEST(CCDEdgeEdgeTests, MovingEdgesCrossInteriorWritesEarliestToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),      //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),      //
        vd(-0.5, 0.5, 1.0), vd(0.0, 0.0, -2.0),    //
        vd(0.5, 0.5, 1.0), vd(0.0, 0.0, -2.0), toi //
    ));
    EXPECT_NEAR(toi, 0.5, ToiTolerance);
}

TEST(CCDEdgeEdgeTests, SeparatedEdgesLeaveToiUnchanged) {
    double toi = UnchangedToi;
    EXPECT_FALSE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 1.0, 0.0), vd(0.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, UnchangedToi);
}

TEST(CCDEdgeEdgeTests, InitialEndpointContactWritesZeroToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 1.0, 0.0), vd(0.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, 0.0);
}

TEST(CCDEdgeEdgeTests, FinalEndpointContactWritesOneToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(2.0, 0.0, 0.0), vd(-1.0, 0.0, 0.0), //
        vd(2.0, 1.0, 0.0), vd(-1.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_NEAR(toi, 1.0, ToiTolerance);
}

TEST(CCDEdgeEdgeTests, InitialCollinearOverlapWritesZeroToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(0.5, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.5, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, 0.0);
}

TEST(CCDEdgeEdgeTests, ParallelCoplanarNonOverlapMisses) {
    double toi = UnchangedToi;
    EXPECT_FALSE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0), //
        vd(1.0, 1.0, 0.0), vd(0.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, UnchangedToi);
}

TEST(CCDEdgeEdgeTests, CoplanarMovingEdgeHitsEndpoint) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(0.0, 1.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(-1.0, 0.5, 0.0), vd(1.0, 0.0, 0.0), //
        vd(-0.5, 0.5, 0.0), vd(1.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_NEAR(toi, 0.5, ToiTolerance);
}

TEST(CCDEdgeEdgeTests, CollinearMovingEdgeWritesFirstOverlapToi) {
    double toi = -1.0;
    EXPECT_TRUE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(1.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(2.0, 0.0, 0.0), vd(-2.0, 0.0, 0.0), //
        vd(3.0, 0.0, 0.0), vd(-2.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_NEAR(toi, 0.5, ToiTolerance);
}

TEST(CCDEdgeEdgeTests, DegenerateEdgeLeavesToiUnchanged) {
    double toi = UnchangedToi;
    EXPECT_FALSE(krd::ipc::CCDEdgeEdge(
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(0.0, 0.0, 0.0), vd(0.0, 0.0, 0.0),  //
        vd(-1.0, 0.0, 0.0), vd(2.0, 0.0, 0.0), //
        vd(-1.0, 1.0, 0.0), vd(2.0, 0.0, 0.0), //
        toi
    ));
    EXPECT_DOUBLE_EQ(toi, UnchangedToi);
}
