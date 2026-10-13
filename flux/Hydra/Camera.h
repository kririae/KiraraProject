#pragma once

#include <pxr/base/gf/matrix4d.h>

namespace flux {
class Camera;
} // namespace flux

/// \brief Sets \p camera to see what a Hydra camera sees on a film.
///
/// \p worldToView and \p projection are the matrices of the Hydra camera.
/// \p filmAspect is the width of the film over its height.
/// The camera sits at the view's origin and looks down its -Z axis, with +Y up.
/// Its field of view and screen window follow \p projection.
/// Its lens does not change.
///
/// \pre \p worldToView is invertible.
/// \pre \p projection is a perspective projection.
/// \pre \p filmAspect is positive.
void HdFluxSetCamera(
    flux::Camera &camera, pxr::GfMatrix4d const &worldToView, pxr::GfMatrix4d const &projection,
    double filmAspect
);
