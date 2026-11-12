#pragma once

#include <pxr/imaging/hd/sceneDelegate.h>

#include "flux/Core/Math.h"

/// \brief Returns the emission of light \p id, which UsdLux defines, before \c normalize divides it
/// by the area.
[[nodiscard]] flux::Spectrum
HdFluxComputeLightEmission(pxr::HdSceneDelegate &sceneDelegate, pxr::SdfPath const &id);
