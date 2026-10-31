#include "flux/Hydra/Material.h"

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <exception>

#include "flux/Hydra/PreviewSurface.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "kira/Properties.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    Tokens,
    (UsdPreviewSurface)
    (diffuseColor)
    (roughness)
    (metallic)
    (ior)
);
// clang-format on

/// Returns the preview surface behind the \c surface terminal, or the defaults of one.
[[nodiscard]] HdFluxPreviewSurface
readPreviewSurface(HdSceneDelegate &sceneDelegate, SdfPath const &id) {
    HdFluxPreviewSurface surface;

    auto const resource = sceneDelegate.GetMaterialResource(id);
    if (!resource.IsHolding<HdMaterialNetworkMap>())
        return surface;

    auto const network =
        HdConvertToHdMaterialNetwork2(resource.UncheckedGet<HdMaterialNetworkMap>());
    auto const terminal = network.terminals.find(HdMaterialTerminalTokens->surface);
    if (terminal == network.terminals.end())
        return surface;

    auto const node = network.nodes.find(terminal->second.upstreamNode);
    if (node == network.nodes.end() || node->second.nodeTypeId != Tokens->UsdPreviewSurface)
        return surface;

    auto const read = [&]<typename T>(TfToken const &name, T const &fallback) {
        auto const parameter = node->second.parameters.find(name);
        if (parameter == node->second.parameters.end())
            return fallback;
        return parameter->second.GetWithDefault(fallback);
    };
    surface.diffuseColor = read(Tokens->diffuseColor, surface.diffuseColor);
    surface.roughness = read(Tokens->roughness, surface.roughness);
    surface.metallic = read(Tokens->metallic, surface.metallic);
    surface.ior = read(Tokens->ior, surface.ior);
    return surface;
}
} // namespace

HdFluxMaterial::HdFluxMaterial(SdfPath const &id) : HdMaterial(id) {}

HdFluxMaterial::~HdFluxMaterial() = default;

void HdFluxMaterial::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    try {
        auto const context = param.GetContext();
        if (!bsdf_)
            bsdf_ = context->create<flux::PrincipledBSDF>(kira::Properties{});

        HdFluxWritePreviewSurface(*context, *bsdf_, readPreviewSurface(*sceneDelegate, GetId()));
    } catch (std::exception const &error) { TF_WARN("%s: %s", GetId().GetText(), error.what()); }

    *dirtyBits = Clean;
}

HdDirtyBits HdFluxMaterial::GetInitialDirtyBitsMask() const { return DirtyParams | DirtyResource; }

flux::Ref<flux::BSDF const> HdFluxMaterial::GetBSDF() const { return bsdf_; }
