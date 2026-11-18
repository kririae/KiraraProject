#include "flux/Hydra/ResolvedPath.h"

#include "flux/Core/Logging.h"

PXR_NAMESPACE_USING_DIRECTIVE

std::string HdFLuxGetResolvedPath(SdfAssetPath const &asset, SdfPath const &prim) {
    if (asset.GetResolvedPath().empty() && !asset.GetAssetPath().empty())
        flux::LogWarn(
            "{}: the asset path '{}' does not resolve", prim.GetString(), asset.GetAssetPath()
        );
    return asset.GetResolvedPath();
}
