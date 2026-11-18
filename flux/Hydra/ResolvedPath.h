#pragma once

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>

#include <string>

[[nodiscard]] std::string
HdFLuxGetResolvedPath(PXR_NS::SdfAssetPath const &asset, PXR_NS::SdfPath const &prim);
