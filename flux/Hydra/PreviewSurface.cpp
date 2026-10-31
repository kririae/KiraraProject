#include "flux/Hydra/PreviewSurface.h"

#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Texture.h"
#include "kira/Properties.h"

void HdFluxWritePreviewSurface(
    flux::Context &context, flux::PrincipledBSDF &bsdf, HdFluxPreviewSurface const &surface
) {
    auto const makeTexture = [&](auto const &value) {
        kira::Properties props;
        props.set("value", value);
        return context.create<flux::ConstantTexture>(props);
    };

    auto const baseColor = makeTexture(
        flux::Spectrum{surface.diffuseColor[0], surface.diffuseColor[1], surface.diffuseColor[2]}
    );
    auto const roughness = makeTexture(surface.roughness);
    auto const metallic = makeTexture(surface.metallic);
    auto const zero = makeTexture(0.0F);

    bsdf.setBaseColor(baseColor);
    bsdf.setRoughness(roughness);
    bsdf.setMetallic(metallic);
    bsdf.setSpecTrans(zero);
    bsdf.setSpecTint(zero);
    bsdf.setSheen(zero);
    bsdf.setSheenTint(zero);
    bsdf.setFlatness(zero);
    bsdf.setClearcoat(zero);
    bsdf.setClearcoatRoughness(zero);
    bsdf.setEta(surface.ior);
}
