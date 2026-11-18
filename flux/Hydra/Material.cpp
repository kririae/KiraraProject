#include "flux/Hydra/Material.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/work/loops.h>
#include <pxr/imaging/hd/sceneDelegate.h>

#include <cstddef>
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
[[nodiscard]] std::optional<flux::ShaderInstance> generateShader(
    HdFLuxRenderParam const &param, SdfPath const &id, HdFLuxMaterialXXml const &materialXml
) {
    auto generation =
        param.GetShaderGenerator().generate(materialXml.xml, materialXml.surfaceMaterialName, {});
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

void setShader(
    flux::Context &context, flux::ShaderBSDF &bsdf, flux::ShaderEDF &edf,
    flux::ShaderInstance const &instance
) {
    auto const textures = createTextures(context, instance.textures);
    bsdf.setShader(instance.spec, instance.words, textures);
    edf.setShader(instance.spec, instance.words, textures);
}

[[nodiscard]] std::vector<std::optional<HdFLuxMaterialXXml>>
toMaterialXXmls(std::vector<HdFLuxPendingMaterial> const &materials) {
    std::vector<std::optional<HdFLuxMaterialXXml>> materialXmls(materials.size());
    WorkParallelForN(materials.size(), [&](std::size_t begin, std::size_t end) {
        for (auto index = begin; index < end; ++index) {
            auto const &material = materials[index];
            try {
                materialXmls[index] = HdFLuxToMaterialXXml(material.network, material.id);
            } catch (std::exception const &error) {
                TF_WARN("%s: %s", material.id.GetText(), error.what());
            }
        }
    });
    return materialXmls;
}
} // namespace

HdFLuxMaterial::HdFLuxMaterial(SdfPath const &id) : HdMaterial(id) {}

HdFLuxMaterial::~HdFLuxMaterial() = default;

void HdFLuxMaterial::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();
    auto &param = *static_cast<HdFLuxRenderParam *>(renderParam);

    try {
        auto const context = param.GetContext();
        auto const &fallback = param.GetFallbackShaderInstance();
        if (!bsdf_) {
            edf_ = context->create<flux::ShaderEDF>(fallback.spec, fallback.words);
            bsdf_ = context->create<flux::ShaderBSDF>(fallback.spec, fallback.words);
        }

        auto const resource = sceneDelegate->GetMaterialResource(id);
        auto network =
            resource.IsHolding<HdMaterialNetworkMap>()
                ? HdConvertToHdMaterialNetwork2(resource.UncheckedGet<HdMaterialNetworkMap>())
                : HdMaterialNetwork2{};
        param.AddPendingMaterial({id, std::move(network), bsdf_, edf_});
    } catch (std::exception const &error) { TF_WARN("%s: %s", id.GetText(), error.what()); }

    *dirtyBits = Clean;
}

HdDirtyBits HdFLuxMaterial::GetInitialDirtyBitsMask() const { return DirtyParams | DirtyResource; }

flux::Ref<flux::BSDF const> HdFLuxMaterial::GetBSDF() const { return bsdf_; }

flux::Ref<flux::EDF const> HdFLuxMaterial::GetEDF() const { return edf_; }

void HdFLuxCommitMaterials(HdFLuxRenderParam &param) {
    auto const materials = param.ExtractPendingMaterials();
    auto const materialXmls = toMaterialXXmls(materials);

    auto const context = param.GetContext();
    for (std::size_t index = 0; index < materials.size(); ++index) {
        auto const &material = materials[index];
        auto const &materialXml = materialXmls[index];
        try {
            auto const generated =
                materialXml ? generateShader(param, material.id, *materialXml) : std::nullopt;
            auto const &instance = generated ? *generated : param.GetFallbackShaderInstance();
            setShader(*context, *material.bsdf, *material.edf, instance);
        } catch (std::exception const &error) {
            TF_WARN("%s: %s", material.id.GetText(), error.what());
        }
    }
}
