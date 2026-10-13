#include <gtest/gtest.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range2d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3d.h>

#include "flux/Hydra/Camera.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/CameraImpl.h"

// The FLux ray through each corner pixel of a non-square film is the ray of the Hydra camera
// through the same raster position. The Hydra ray comes from the inverse of the view-projection.
TEST(HydraCameraTests, CastsTheRaysOfAnOffCenterProjection) {
    constexpr unsigned width{64U};
    constexpr unsigned height{32U};

    // The camera is far from the origin, and its window is off-center and not conformed to the
    // film.
    pxr::GfFrustum frustum;
    frustum.SetPosition(pxr::GfVec3d(3000.37, -2900.61, 2200.23));
    frustum.SetRotation(pxr::GfRotation(pxr::GfVec3d(1.0, 2.0, 0.5), 35.0));
    frustum.SetProjectionType(pxr::GfFrustum::Perspective);
    frustum.SetWindow(pxr::GfRange2d(pxr::GfVec2d(-0.5, -0.5), pxr::GfVec2d(1.1, 0.5)));
    frustum.SetNearFar(pxr::GfRange1d(0.5, 100.0));
    auto const worldToView = frustum.ComputeViewMatrix();
    auto const projection = frustum.ComputeProjectionMatrix();

    auto const camera = flux::Camera::create();
    HdFluxSetCamera(*camera, worldToView, projection, static_cast<double>(width) / height);
    auto const impl = camera->getImpl();

    auto const inverse = (worldToView * projection).GetInverse();
    auto const position = frustum.GetPosition();
    for (auto const x : {0U, width - 1U}) {
        for (auto const y : {0U, height - 1U}) {
            auto const ndcX = 2.0 * x / width - 1.0;
            auto const ndcY = 1.0 - 2.0 * y / height;

            auto const expected =
                (inverse.Transform(pxr::GfVec3d(ndcX, ndcY, 0.5)) - position).GetNormalized();
            auto const ray = impl.generateRay(
                {static_cast<float>(x), static_cast<float>(y)}, {0.5F, 0.5F}, {width, height}
            );

            EXPECT_NEAR(ray.origin.x(), position[0], 1.0e-3);
            EXPECT_NEAR(ray.origin.y(), position[1], 1.0e-3);
            EXPECT_NEAR(ray.origin.z(), position[2], 1.0e-3);

            EXPECT_NEAR(ray.direction.x(), expected[0], 1.0e-5);
            EXPECT_NEAR(ray.direction.y(), expected[1], 1.0e-5);
            EXPECT_NEAR(ray.direction.z(), expected[2], 1.0e-5);
        }
    }
}
