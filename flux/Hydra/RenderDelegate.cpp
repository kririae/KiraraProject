#include "flux/Hydra/RenderDelegate.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sprim.h>
#include <pxr/imaging/hd/tokens.h>

#include <cstdint>
#include <memory>
#include <string>

#include "flux/Hydra/AovMapping.h"
#include "flux/Hydra/AreaLight.h"
#include "flux/Hydra/DomeLight.h"
#include "flux/Hydra/Material.h"
#include "flux/Hydra/Mesh.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderPass.h"
#include "flux/Hydra/RenderSettings.h"
#include "flux/Scene/Context.h"
#include "kira/Compiler.h"
#include "kira/Properties.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdFluxRenderSettingsTokens, HDFLUX_RENDER_SETTINGS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    Tokens,
    (mtlx)
    (independent)
    (sobol)
);
// clang-format on

pxr::TfToken toToken(flux::SamplerType type) {
    switch (type) {
    case flux::SamplerType::Independent: return Tokens->independent;
    case flux::SamplerType::Sobol: return Tokens->sobol;
    }
    KIRA_UNREACHABLE();
}

std::uint32_t getDepthSetting(
    pxr::HdRenderDelegate const &delegate, pxr::TfToken const &key, std::uint32_t defaultValue
) {
    auto const value = delegate.GetRenderSetting<int>(key, static_cast<int>(defaultValue));
    if (value < 0) {
        TF_WARN("%s is %d, using 0", key.GetText(), value);
        return 0;
    }
    return static_cast<std::uint32_t>(value);
}

pxr::TfToken
getSamplerSetting(pxr::HdRenderDelegate const &delegate, pxr::TfToken const &defaultValue) {
    auto const value = delegate.GetRenderSetting(HdFluxRenderSettingsTokens->sampler);
    auto const name =
        value.IsHolding<pxr::TfToken>()
            ? value.UncheckedGet<pxr::TfToken>()
            : pxr::TfToken{value.GetWithDefault<std::string>(defaultValue.GetString())};
    if (name == Tokens->independent || name == Tokens->sobol)
        return name;

    TF_WARN(
        "%s is '%s', using '%s'", HdFluxRenderSettingsTokens->sampler.GetText(), name.GetText(),
        defaultValue.GetText()
    );
    return defaultValue;
}
} // namespace

HdFluxRenderDelegate::HdFluxRenderDelegate(
    std::filesystem::path const &modulePath, std::filesystem::path const &mtlxRoot,
    pxr::HdRenderSettingsMap const &settingsMap
)
    : pxr::HdRenderDelegate(settingsMap),
      resourceRegistry_(std::make_shared<pxr::HdResourceRegistry>()),
      renderParam_(std::make_unique<HdFluxRenderParam>(modulePath, mtlxRoot)),
      defaultIntegrator_(renderParam_->GetContext()->getActiveIntegrator()->getImpl()),
      defaultShaderReorder_(renderParam_->GetContext()->getActiveIntegrator()->usesShaderReorder()),
      defaultSampler_(renderParam_->GetContext()->getActiveSampler()->getType()) {
    _PopulateDefaultSettings(GetRenderSettingDescriptors());
}

HdFluxRenderDelegate::~HdFluxRenderDelegate() = default;

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedRprimTypes() const {
    static auto const Types = pxr::TfTokenVector{pxr::HdPrimTypeTokens->mesh};
    return Types;
}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedSprimTypes() const {
    static auto const Types = pxr::TfTokenVector{
        pxr::HdPrimTypeTokens->camera,      pxr::HdPrimTypeTokens->domeLight,
        pxr::HdPrimTypeTokens->rectLight,   pxr::HdPrimTypeTokens->diskLight,
        pxr::HdPrimTypeTokens->sphereLight, pxr::HdPrimTypeTokens->cylinderLight,
        pxr::HdPrimTypeTokens->material,
    };
    return Types;
}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedBprimTypes() const {
    static auto const Types = pxr::TfTokenVector{pxr::HdPrimTypeTokens->renderBuffer};
    return Types;
}

pxr::TfToken HdFluxRenderDelegate::GetMaterialBindingPurpose() const { return pxr::HdTokens->full; }

pxr::TfTokenVector HdFluxRenderDelegate::GetMaterialRenderContexts() const {
    return {Tokens->mtlx};
}

pxr::HdResourceRegistrySharedPtr HdFluxRenderDelegate::GetResourceRegistry() const {
    return resourceRegistry_;
}

pxr::HdRenderParam *HdFluxRenderDelegate::GetRenderParam() const { return renderParam_.get(); }

pxr::HdRenderPassSharedPtr HdFluxRenderDelegate::CreateRenderPass(
    pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection
) {
    return std::make_shared<HdFluxRenderPass>(index, collection, *renderParam_);
}

pxr::HdInstancer *
HdFluxRenderDelegate::CreateInstancer(pxr::HdSceneDelegate *, pxr::SdfPath const &) {
    return nullptr;
}

void HdFluxRenderDelegate::DestroyInstancer(pxr::HdInstancer *instancer) { delete instancer; }

pxr::HdRprim *
HdFluxRenderDelegate::CreateRprim(pxr::TfToken const &typeId, pxr::SdfPath const &rprimId) {
    if (typeId == pxr::HdPrimTypeTokens->mesh)
        return new HdFluxMesh(rprimId);
    return nullptr;
}

void HdFluxRenderDelegate::DestroyRprim(pxr::HdRprim *rprim) { delete rprim; }

pxr::HdSprim *
HdFluxRenderDelegate::CreateSprim(pxr::TfToken const &typeId, pxr::SdfPath const &sprimId) {
    if (typeId == pxr::HdPrimTypeTokens->camera)
        return new pxr::HdCamera(sprimId);
    if (typeId == pxr::HdPrimTypeTokens->domeLight)
        return new HdFluxDomeLight(sprimId);
    if (typeId == pxr::HdPrimTypeTokens->rectLight || typeId == pxr::HdPrimTypeTokens->diskLight ||
        typeId == pxr::HdPrimTypeTokens->sphereLight ||
        typeId == pxr::HdPrimTypeTokens->cylinderLight)
        return new HdFluxAreaLight(sprimId, typeId);
    if (typeId == pxr::HdPrimTypeTokens->material)
        return new HdFluxMaterial(sprimId);
    return nullptr;
}

pxr::HdSprim *HdFluxRenderDelegate::CreateFallbackSprim(pxr::TfToken const &typeId) {
    return CreateSprim(typeId, pxr::SdfPath::EmptyPath());
}

void HdFluxRenderDelegate::DestroySprim(pxr::HdSprim *sprim) { delete sprim; }

pxr::HdBprim *
HdFluxRenderDelegate::CreateBprim(pxr::TfToken const &typeId, pxr::SdfPath const &bprimId) {
    if (typeId == pxr::HdPrimTypeTokens->renderBuffer)
        return new HdFluxRenderBuffer(bprimId);
    return nullptr;
}

pxr::HdBprim *HdFluxRenderDelegate::CreateFallbackBprim(pxr::TfToken const &typeId) {
    return CreateBprim(typeId, pxr::SdfPath::EmptyPath());
}

void HdFluxRenderDelegate::DestroyBprim(pxr::HdBprim *bprim) { delete bprim; }

void HdFluxRenderDelegate::CommitResources(pxr::HdChangeTracker *) try {
    if (renderParam_->HasFailed())
        return;

    auto const context = renderParam_->GetContext();
    if (auto const version = GetRenderSettingsVersion(); settingsVersion_ != version) {
        settingsVersion_ = version;
        syncIntegrator(*context);
        syncSampler(*context);
    }
    context->collectGarbage();
    renderParam_->GetHandler().sync();

    // It is the host's role to manually clear dirty state since multiple handlers might consume the
    // same context.
    context->clearDirty();
} catch (...) { renderParam_->ReportFailure(); }

void HdFluxRenderDelegate::syncIntegrator(flux::Context &context) const {
    auto const maxDepth =
        getDepthSetting(*this, HdFluxRenderSettingsTokens->maxDepth, defaultIntegrator_.maxDepth);
    auto const rrDepth =
        getDepthSetting(*this, HdFluxRenderSettingsTokens->rrDepth, defaultIntegrator_.rrDepth);
    auto const rrProb =
        GetRenderSetting<float>(HdFluxRenderSettingsTokens->rrProb, defaultIntegrator_.rrProb);
    auto const shaderReorder =
        GetRenderSetting<bool>(HdFluxRenderSettingsTokens->shaderReorder, defaultShaderReorder_);

    auto const integrator = context.getActiveIntegrator();
    if (auto const active = integrator->getImpl();
        active.maxDepth != maxDepth || active.rrDepth != rrDepth || active.rrProb != rrProb ||
        integrator->usesShaderReorder() != shaderReorder) {
        auto props = kira::Properties{};
        props.set("max_depth", maxDepth);
        props.set("rr_depth", rrDepth);
        props.set("rr_prob", rrProb);
        props.set("shader_reorder", shaderReorder);
        context.setActiveIntegrator(context.create<flux::PathIntegrator>(props));
        context.remove(integrator->getContextId());
    }
}

void HdFluxRenderDelegate::syncSampler(flux::Context &context) const {
    auto const sampler = context.getActiveSampler();
    if (auto const name = getSamplerSetting(*this, toToken(defaultSampler_));
        toToken(sampler->getType()) != name) {
        auto props = kira::Properties{};
        props.set("type", name.GetString());
        context.setActiveSampler(context.create<flux::Sampler>(props));
        context.remove(sampler->getContextId());
    }
}

pxr::HdAovDescriptor HdFluxRenderDelegate::GetDefaultAovDescriptor(pxr::TfToken const &name) const {
    return HdFluxGetAovDescriptor(name);
}

pxr::HdRenderSettingDescriptorList HdFluxRenderDelegate::GetRenderSettingDescriptors() const {
    return {
        {.name = "Converged Samples Per Pixel",
         .key = pxr::HdRenderSettingsTokens->convergedSamplesPerPixel,
         .defaultValue = pxr::VtValue(HdFluxDefaultConvergedSamplesPerPixel)},
        {.name = "Max Depth",
         .key = HdFluxRenderSettingsTokens->maxDepth,
         .defaultValue = pxr::VtValue(static_cast<int>(defaultIntegrator_.maxDepth))},
        {.name = "Russian Roulette Depth",
         .key = HdFluxRenderSettingsTokens->rrDepth,
         .defaultValue = pxr::VtValue(static_cast<int>(defaultIntegrator_.rrDepth))},
        {.name = "Russian Roulette Probability",
         .key = HdFluxRenderSettingsTokens->rrProb,
         .defaultValue = pxr::VtValue(defaultIntegrator_.rrProb)},
        {.name = "Sampler",
         .key = HdFluxRenderSettingsTokens->sampler,
         .defaultValue = pxr::VtValue(toToken(defaultSampler_))},
        {.name = "Shader Execution Reordering",
         .key = HdFluxRenderSettingsTokens->shaderReorder,
         .defaultValue = pxr::VtValue(defaultShaderReorder_)},
    };
}

pxr::VtDictionary HdFluxRenderDelegate::GetRenderStats() const {
    pxr::VtDictionary stats;
    if (renderParam_->HasFailed())
        stats["huskErrorStatus"] = 1;
    return stats;
}
