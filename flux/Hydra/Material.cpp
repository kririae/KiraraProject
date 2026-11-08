#include "flux/Hydra/Material.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <exception>
#include <optional>
#include <utility>
#include <vector>

#include "flux/Hydra/MaterialXXml.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/Texture.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
[[nodiscard]] std::optional<HdFluxMaterialXGenerator::Instance> generateMaterial(
    HdSceneDelegate &sceneDelegate, HdFluxRenderParam const &param, SdfPath const &id
) {
    auto const resource = sceneDelegate.GetMaterialResource(id);
    if (!resource.IsHolding<HdMaterialNetworkMap>())
        return std::nullopt;

    auto const network =
        HdConvertToHdMaterialNetwork2(resource.UncheckedGet<HdMaterialNetworkMap>());
    auto const terminal = network.terminals.find(HdMaterialTerminalTokens->surface);
    if (terminal == network.terminals.end())
        return std::nullopt;

    auto const materialXml = HdFluxToMaterialXXml(network, terminal->second.upstreamNode, id);
    if (!materialXml)
        return std::nullopt;

    // TODO: generate the material with constant values at its first sync, and make an input that
    // a later edit changes interactive.
    return param.GetMaterialXGenerator().generate(
        materialXml->xml, materialXml->surfaceMaterialName
    );
}

void setShader(
    flux::ShaderBSDF &bsdf, HdFluxRenderParam const &param,
    std::optional<HdFluxMaterialXGenerator::Instance> instance
) {
    if (!instance) {
        auto const &fallback = param.GetFallbackShader();
        bsdf.setShader(fallback.spec, fallback.words, {});
        return;
    }

    auto const context = param.GetContext();
    std::vector<flux::Ref<flux::ImageTexture const>> textures;
    for (auto const &spec : instance->textures)
        textures.push_back(context->create<flux::ImageTexture>(spec));

    bsdf.setShader(instance->spec, std::move(instance->words), std::move(textures));
}
} // namespace

HdFluxMaterial::HdFluxMaterial(SdfPath const &id) : HdMaterial(id) {}

HdFluxMaterial::~HdFluxMaterial() = default;

void HdFluxMaterial::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFluxRenderParam *>(renderParam);

    try {
        if (!bsdf_) {
            auto const &fallback = param.GetFallbackShader();
            bsdf_ = param.GetContext()->create<flux::ShaderBSDF>(fallback.spec, fallback.words);
        }

        setShader(*bsdf_, param, generateMaterial(*sceneDelegate, param, id));
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits = Clean;
}

HdDirtyBits HdFluxMaterial::GetInitialDirtyBitsMask() const { return DirtyParams | DirtyResource; }

flux::Ref<flux::BSDF const> HdFluxMaterial::GetBSDF() const { return bsdf_; }
