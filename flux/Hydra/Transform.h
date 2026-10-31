#pragma once

#include <pxr/base/gf/matrix4d.h>

#include <array>

/// \brief Returns the row-major 3x4 matrix of \c flux::Primitive that maps column vectors as
/// \p matrix maps row vectors.
[[nodiscard]] std::array<float, 12> HdFluxToTransform(pxr::GfMatrix4d const &matrix);
