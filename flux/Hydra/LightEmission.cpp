#include "flux/Hydra/LightEmission.h"

#include <pxr/base/gf/color.h>
#include <pxr/base/gf/colorSpace.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/light.h>

#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
[[nodiscard]] flux::Spectrum blackbody(float kelvin) {
    static GfColorSpace const rec709{GfColorSpaceNames->LinearRec709};
    static constexpr GfVec3f rec709Luminance{0.2126F, 0.7152F, 0.0722F};

    GfColor color{rec709};
    // The range of UsdLuxBlackbodyTemperatureAsRgb.
    color.SetFromPlanckianLocus(std::clamp(kelvin, 1000.0F, 10000.0F), 1.0F);
    auto const rgb = color.GetRGB() / GfDot(color.GetRGB(), rec709Luminance);
    return {rgb[0], rgb[1], rgb[2]};
}
} // namespace

flux::Spectrum HdFluxComputeLightEmission(HdSceneDelegate &sceneDelegate, SdfPath const &id) {
    auto const get = [&]<typename T>(TfToken const &token, T const &fallback) {
        return sceneDelegate.GetLightParamValue(id, token).GetWithDefault(fallback);
    };

    auto const color = get(HdLightTokens->color, GfVec3f{1.0F});
    auto const scale =
        get(HdLightTokens->intensity, 1.0F) * std::exp2(get(HdLightTokens->exposure, 0.0F));
    auto emission = flux::Spectrum{color[0], color[1], color[2]} * scale;

    if (get(HdLightTokens->enableColorTemperature, false))
        emission = emission * blackbody(get(HdLightTokens->colorTemperature, 6500.0F));
    return emission;
}
