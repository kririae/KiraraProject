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
#include <optional>
#include <string>

#include "flux/Hydra/AovMapping.h"
#include "flux/Hydra/AreaLight.h"
#include "flux/Hydra/DomeLight.h"
#include "flux/Hydra/Instancer.h"
#include "flux/Hydra/Material.h"
#include "flux/Hydra/Mesh.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderPass.h"
#include "flux/Hydra/RenderSettings.h"
#include "flux/Hydra/Renderer.h"
#include "flux/Scene/Context.h"
#include "kira/Compiler.h"
#include "kira/Properties.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdFLuxRenderSettingsTokens, HDFLUX_RENDER_SETTINGS_TOKENS);

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

HdFLuxSampleSchedule makeSchedule(pxr::HdRenderDelegate const &delegate) {
    static pxr::TfToken const huskRenderMode{"renderMode"};
    auto const batch =
        !delegate.GetRenderSetting<bool>(pxr::HdRenderSettingsTokens->enableInteractive, true) ||
        delegate.GetRenderSetting<std::string>(huskRenderMode, {}) == "batch";
    auto const target = delegate.GetRenderSetting<int>(
        pxr::HdRenderSettingsTokens->convergedSamplesPerPixel, HdFLuxDefaultConvergedSamplesPerPixel
    );

    std::optional<HdFLuxSampleSchedule::DenoiseCadence> denoiseCadence;
    if (delegate.GetRenderSetting<bool>(HdFLuxRenderSettingsTokens->denoise, HdFLuxDefaultDenoise))
        denoiseCadence = HdFLuxSampleSchedule::DenoiseCadence{
            .startSample = batch ? target
                                 : delegate.GetRenderSetting<int>(
                                       HdFLuxRenderSettingsTokens->denoiseStartSample,
                                       HdFLuxDefaultDenoiseStartSample
                                   ),
            .launchesPerDenoise = delegate.GetRenderSetting<int>(
                HdFLuxRenderSettingsTokens->launchesPerDenoise, HdFLuxDefaultLaunchesPerDenoise
            ),
        };

    return {
        target,
        delegate.GetRenderSetting<int>(
            HdFLuxRenderSettingsTokens->interactiveSamplesPerLaunch,
            HdFLuxDefaultInteractiveSamplesPerLaunch
        ),
        batch ? HdFLuxSampleSchedule::Mode::Batch : HdFLuxSampleSchedule::Mode::Interactive,
        denoiseCadence,
    };
}

pxr::TfToken
getSamplerSetting(pxr::HdRenderDelegate const &delegate, pxr::TfToken const &defaultValue) {
    auto const value = delegate.GetRenderSetting(HdFLuxRenderSettingsTokens->sampler);
    auto const name =
        value.IsHolding<pxr::TfToken>()
            ? value.UncheckedGet<pxr::TfToken>()
            : pxr::TfToken{value.GetWithDefault<std::string>(defaultValue.GetString())};
    if (name == Tokens->independent || name == Tokens->sobol)
        return name;

    TF_WARN(
        "%s is '%s', using '%s'", HdFLuxRenderSettingsTokens->sampler.GetText(), name.GetText(),
        defaultValue.GetText()
    );
    return defaultValue;
}
} // namespace

HdFLuxRenderDelegate::HdFLuxRenderDelegate(
    std::filesystem::path const &mtlxRoot, pxr::HdRenderSettingsMap const &settingsMap
)
    : pxr::HdRenderDelegate(settingsMap),
      resourceRegistry_(std::make_shared<pxr::HdResourceRegistry>()),
      renderParam_(std::make_unique<HdFLuxRenderParam>(mtlxRoot, renderThread_)),
      renderer_(std::make_unique<HdFLuxRenderer>(*renderParam_, renderThread_)),
      schedule_(HdFLuxDefaultConvergedSamplesPerPixel, HdFLuxDefaultInteractiveSamplesPerLaunch),
      defaultIntegrator_(renderParam_->GetContext()->getActiveIntegrator()->getImpl()),
      defaultShaderReorder_(renderParam_->GetContext()->getActiveIntegrator()->usesShaderReorder()),
      defaultSampler_(renderParam_->GetContext()->getActiveSampler()->getType()) {
    _PopulateDefaultSettings(GetRenderSettingDescriptors());

    renderThread_.SetRenderCallback([renderer = renderer_.get()] { renderer->Render(); });
    renderThread_.StartThread();
}

HdFLuxRenderDelegate::~HdFLuxRenderDelegate() { renderThread_.StopThread(); }

pxr::TfTokenVector const &HdFLuxRenderDelegate::GetSupportedRprimTypes() const {
    static auto const Types = pxr::TfTokenVector{pxr::HdPrimTypeTokens->mesh};
    return Types;
}

pxr::TfTokenVector const &HdFLuxRenderDelegate::GetSupportedSprimTypes() const {
    static auto const Types = pxr::TfTokenVector{
        pxr::HdPrimTypeTokens->camera,      pxr::HdPrimTypeTokens->domeLight,
        pxr::HdPrimTypeTokens->rectLight,   pxr::HdPrimTypeTokens->diskLight,
        pxr::HdPrimTypeTokens->sphereLight, pxr::HdPrimTypeTokens->cylinderLight,
        pxr::HdPrimTypeTokens->material,
    };
    return Types;
}

pxr::TfTokenVector const &HdFLuxRenderDelegate::GetSupportedBprimTypes() const {
    static auto const Types = pxr::TfTokenVector{pxr::HdPrimTypeTokens->renderBuffer};
    return Types;
}

pxr::TfToken HdFLuxRenderDelegate::GetMaterialBindingPurpose() const { return pxr::HdTokens->full; }

pxr::TfTokenVector HdFLuxRenderDelegate::GetMaterialRenderContexts() const {
    return {Tokens->mtlx};
}

pxr::HdResourceRegistrySharedPtr HdFLuxRenderDelegate::GetResourceRegistry() const {
    return resourceRegistry_;
}

pxr::HdRenderParam *HdFLuxRenderDelegate::GetRenderParam() const { return renderParam_.get(); }

pxr::HdRenderPassSharedPtr HdFLuxRenderDelegate::CreateRenderPass(
    pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection
) {
    return std::make_shared<HdFLuxRenderPass>(index, collection, *renderParam_, *renderer_);
}

pxr::HdInstancer *
HdFLuxRenderDelegate::CreateInstancer(pxr::HdSceneDelegate *delegate, pxr::SdfPath const &id) {
    return new HdFLuxInstancer(delegate, id);
}

void HdFLuxRenderDelegate::DestroyInstancer(pxr::HdInstancer *instancer) { delete instancer; }

pxr::HdRprim *
HdFLuxRenderDelegate::CreateRprim(pxr::TfToken const &typeId, pxr::SdfPath const &rprimId) {
    if (typeId == pxr::HdPrimTypeTokens->mesh)
        return new HdFLuxMesh(rprimId);
    return nullptr;
}

void HdFLuxRenderDelegate::DestroyRprim(pxr::HdRprim *rprim) { delete rprim; }

pxr::HdSprim *
HdFLuxRenderDelegate::CreateSprim(pxr::TfToken const &typeId, pxr::SdfPath const &sprimId) {
    if (typeId == pxr::HdPrimTypeTokens->camera)
        return new pxr::HdCamera(sprimId);
    if (typeId == pxr::HdPrimTypeTokens->domeLight)
        return new HdFLuxDomeLight(sprimId);
    if (typeId == pxr::HdPrimTypeTokens->rectLight || typeId == pxr::HdPrimTypeTokens->diskLight ||
        typeId == pxr::HdPrimTypeTokens->sphereLight ||
        typeId == pxr::HdPrimTypeTokens->cylinderLight)
        return new HdFLuxAreaLight(sprimId, typeId);
    if (typeId == pxr::HdPrimTypeTokens->material)
        return new HdFLuxMaterial(sprimId);
    return nullptr;
}

pxr::HdSprim *HdFLuxRenderDelegate::CreateFallbackSprim(pxr::TfToken const &typeId) {
    return CreateSprim(typeId, pxr::SdfPath::EmptyPath());
}

void HdFLuxRenderDelegate::DestroySprim(pxr::HdSprim *sprim) { delete sprim; }

pxr::HdBprim *
HdFLuxRenderDelegate::CreateBprim(pxr::TfToken const &typeId, pxr::SdfPath const &bprimId) {
    if (typeId == pxr::HdPrimTypeTokens->renderBuffer)
        return new HdFLuxRenderBuffer(bprimId);
    return nullptr;
}

pxr::HdBprim *HdFLuxRenderDelegate::CreateFallbackBprim(pxr::TfToken const &typeId) {
    return CreateBprim(typeId, pxr::SdfPath::EmptyPath());
}

void HdFLuxRenderDelegate::DestroyBprim(pxr::HdBprim *bprim) { delete bprim; }

void HdFLuxRenderDelegate::CommitResources(pxr::HdChangeTracker *) try {
    HdFLuxCommitMaterials(*renderParam_);
    if (renderParam_->HasFailed())
        return;

    auto const context = renderParam_->GetContext();
    if (auto const version = GetRenderSettingsVersion(); settingsVersion_ != version) {
        settingsVersion_ = version;
        syncIntegrator(*context);
        syncSampler(*context);
        schedule_ = makeSchedule(*this);
    }
    context->collectGarbage();
    if (context->getVersion() != renderParam_->GetHandler().getSyncedVersion()) {
        renderThread_.StopRender();
        renderParam_->GetHandler().sync();
    }

    // It is the host's role to manually clear dirty state since multiple handlers might consume the
    // same context.
    context->clearDirty();
} catch (...) { renderParam_->ReportFailure(); }

bool HdFLuxRenderDelegate::IsPauseSupported() const { return true; }

bool HdFLuxRenderDelegate::Pause() {
    renderThread_.PauseRender();
    return true;
}

bool HdFLuxRenderDelegate::Resume() {
    renderThread_.ResumeRender();
    return true;
}

void HdFLuxRenderDelegate::syncIntegrator(flux::Context &context) const {
    auto const maxDepth =
        getDepthSetting(*this, HdFLuxRenderSettingsTokens->maxDepth, defaultIntegrator_.maxDepth);
    auto const rrDepth =
        getDepthSetting(*this, HdFLuxRenderSettingsTokens->rrDepth, defaultIntegrator_.rrDepth);
    auto const rrProb =
        GetRenderSetting<float>(HdFLuxRenderSettingsTokens->rrProb, defaultIntegrator_.rrProb);
    auto const shaderReorder =
        GetRenderSetting<bool>(HdFLuxRenderSettingsTokens->shaderReorder, defaultShaderReorder_);

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

void HdFLuxRenderDelegate::syncSampler(flux::Context &context) const {
    auto const sampler = context.getActiveSampler();
    if (auto const name = getSamplerSetting(*this, toToken(defaultSampler_));
        toToken(sampler->getType()) != name) {
        auto props = kira::Properties{};
        props.set("type", name.GetString());
        context.setActiveSampler(context.create<flux::Sampler>(props));
        context.remove(sampler->getContextId());
    }
}

pxr::HdAovDescriptor HdFLuxRenderDelegate::GetDefaultAovDescriptor(pxr::TfToken const &name) const {
    return HdFLuxGetAovDescriptor(name);
}

pxr::HdRenderSettingDescriptorList HdFLuxRenderDelegate::GetRenderSettingDescriptors() const {
    return {
        {.name = "Converged Samples Per Pixel",
         .key = pxr::HdRenderSettingsTokens->convergedSamplesPerPixel,
         .defaultValue = pxr::VtValue(HdFLuxDefaultConvergedSamplesPerPixel)},
        {.name = "Max Depth",
         .key = HdFLuxRenderSettingsTokens->maxDepth,
         .defaultValue = pxr::VtValue(static_cast<int>(defaultIntegrator_.maxDepth))},
        {.name = "Russian Roulette Depth",
         .key = HdFLuxRenderSettingsTokens->rrDepth,
         .defaultValue = pxr::VtValue(static_cast<int>(defaultIntegrator_.rrDepth))},
        {.name = "Russian Roulette Probability",
         .key = HdFLuxRenderSettingsTokens->rrProb,
         .defaultValue = pxr::VtValue(defaultIntegrator_.rrProb)},
        {.name = "Sampler",
         .key = HdFLuxRenderSettingsTokens->sampler,
         .defaultValue = pxr::VtValue(toToken(defaultSampler_))},
        {.name = "Shader Execution Reordering",
         .key = HdFLuxRenderSettingsTokens->shaderReorder,
         .defaultValue = pxr::VtValue(defaultShaderReorder_)},
        {.name = "Denoise",
         .key = HdFLuxRenderSettingsTokens->denoise,
         .defaultValue = pxr::VtValue(HdFLuxDefaultDenoise)},
        {.name = "Denoise Start Sample",
         .key = HdFLuxRenderSettingsTokens->denoiseStartSample,
         .defaultValue = pxr::VtValue(HdFLuxDefaultDenoiseStartSample)},
        {.name = "Launches Per Denoise",
         .key = HdFLuxRenderSettingsTokens->launchesPerDenoise,
         .defaultValue = pxr::VtValue(HdFLuxDefaultLaunchesPerDenoise)},
    };
}

pxr::VtDictionary HdFLuxRenderDelegate::GetRenderStats() const {
    pxr::VtDictionary stats;
    stats["rendererName"] = std::string{"FLux"};
    stats["percentDone"] = renderer_->GetPercentDone();
    stats["totalClockTime"] = renderer_->GetTotalClockTime();
    if (renderParam_->HasFailed())
        stats["huskErrorStatus"] = 1;
    return stats;
}
