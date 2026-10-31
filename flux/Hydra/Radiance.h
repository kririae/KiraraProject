#pragma once

#include <pxr/imaging/hd/sceneDelegate.h>

#include "flux/Core/Math.h"

/// \brief Returns the radiance of light \p id before normalization.
[[nodiscard]] flux::Spectrum
HdFluxReadRadiance(pxr::HdSceneDelegate &sceneDelegate, pxr::SdfPath const &id);
