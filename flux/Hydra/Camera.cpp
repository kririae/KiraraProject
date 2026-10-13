#include "flux/Hydra/Camera.h"

#include <pxr/base/gf/vec3d.h>

#include <cmath>
#include <numbers>

#include "flux/Scene/Camera.h"

namespace {
[[nodiscard]] flux::Vec3f toVec3f(pxr::GfVec3d const &vector) {
    return {
        static_cast<float>(vector[0]), static_cast<float>(vector[1]), static_cast<float>(vector[2])
    };
}
} // namespace

void HdFluxSetCamera(
    flux::Camera &camera, pxr::GfMatrix4d const &worldToView, pxr::GfMatrix4d const &projection,
    double filmAspect
) {
    // The inverse of the view places the camera. Its -Z axis looks and its +Y axis is up.
    auto const viewToWorld = worldToView.GetInverse();
    auto const position = viewToWorld.ExtractTranslation();
    auto const forward = viewToWorld.TransformDir(pxr::GfVec3d(0.0, 0.0, -1.0)).GetNormalized();
    auto const up = viewToWorld.TransformDir(pxr::GfVec3d(0.0, 1.0, 0.0)).GetNormalized();

    camera.setPosition(toVec3f(position));
    // The look-at point is far enough that the float rounding of the position keeps the direction.
    camera.setLookAt(toVec3f(position + forward * (1.0 + position.GetLength())));
    camera.setReferenceUp(toVec3f(up));

    // A point at depth 1 in front of the camera has the NDC (P00 x - P20, P11 y - P21). Solving for
    // the NDC range [-1, 1] gives the window, which the film's aspect then rescales in x.
    camera.setVerticalFieldOfView(
        static_cast<float>(2.0 * std::atan(1.0 / projection[1][1]) * 180.0 / std::numbers::pi)
    );

    auto const scaleX = projection[1][1] / (projection[0][0] * filmAspect);
    camera.setScreenWindow({
        .left = static_cast<float>((-1.0 + projection[2][0]) * scaleX),
        .right = static_cast<float>((1.0 + projection[2][0]) * scaleX),
        .bottom = static_cast<float>(-1.0 + projection[2][1]),
        .top = static_cast<float>(1.0 + projection[2][1]),
    });
}
