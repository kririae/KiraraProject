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
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
[[nodiscard]] std::optional<flux::ShaderInstance>
generateShader(HdSceneDelegate &sceneDelegate, HdFluxRenderParam const &param, SdfPath const &id) {
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

    // TODO: generate the shader with constant values at its first sync, and make an input that
    // a later edit changes interactive.
    auto generation =
        param.GetShaderGenerator().generate(materialXml->xml, materialXml->surfaceMaterialName, {});
    for (auto const &diagnostic : generation.diagnostics)
        TF_WARN("%s: %s", id.GetText(), diagnostic.c_str());
    return std::move(generation.instance);
}

[[nodiscard]] std::vector<flux::Ref<flux::ImageTexture const>>
createTextures(flux::Context &context, std::vector<flux::ImageTextureSpec> const &specs) {
    std::vector<flux::Ref<flux::ImageTexture const>> textures;
    for (auto const &spec : specs)
        textures.push_back(context.create<flux::ImageTexture>(spec));
    return textures;
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
        auto const context = param.GetContext();
        auto const &fallback = param.GetFallbackShaderInstance();
        if (!bsdf_) {
            edf_ = context->create<flux::ShaderEDF>(fallback.spec, fallback.words);
            bsdf_ = context->create<flux::ShaderBSDF>(fallback.spec, fallback.words);
        }

        auto const generated = generateShader(*sceneDelegate, param, id);
        auto const &instance = generated ? *generated : fallback;
        auto const textures = createTextures(*context, instance.textures);
        bsdf_->setShader(instance.spec, instance.words, textures);
        edf_->setShader(instance.spec, instance.words, textures);
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits = Clean;
}

HdDirtyBits HdFluxMaterial::GetInitialDirtyBitsMask() const { return DirtyParams | DirtyResource; }

flux::Ref<flux::BSDF const> HdFluxMaterial::GetBSDF() const { return bsdf_; }

flux::Ref<flux::EDF const> HdFluxMaterial::GetEDF() const { return edf_; }
