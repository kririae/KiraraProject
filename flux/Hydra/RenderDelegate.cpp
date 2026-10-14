#include "flux/Hydra/RenderDelegate.h"

#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sprim.h>
#include <pxr/imaging/hd/tokens.h>

#include <memory>

#include "flux/Hydra/AovMapping.h"
#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderParam.h"
#include "flux/Hydra/RenderPass.h"
#include "flux/Hydra/RenderSettings.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdFluxRenderSettingsTokens, HDFLUX_RENDER_SETTINGS_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

HdFluxRenderDelegate::HdFluxRenderDelegate(
    std::filesystem::path const &modulePath, pxr::HdRenderSettingsMap const &settingsMap
)
    : pxr::HdRenderDelegate(settingsMap),
      resourceRegistry_(std::make_shared<pxr::HdResourceRegistry>()),
      renderParam_(std::make_unique<HdFluxRenderParam>(modulePath)) {
    _PopulateDefaultSettings(GetRenderSettingDescriptors());
}

HdFluxRenderDelegate::~HdFluxRenderDelegate() = default;

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedRprimTypes() const {
    static auto const types = pxr::TfTokenVector{};
    return types;
}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedSprimTypes() const {
    static auto const types = pxr::TfTokenVector{pxr::HdPrimTypeTokens->camera};
    return types;
}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedBprimTypes() const {
    static auto const types = pxr::TfTokenVector{pxr::HdPrimTypeTokens->renderBuffer};
    return types;
}

pxr::HdResourceRegistrySharedPtr HdFluxRenderDelegate::GetResourceRegistry() const {
    return resourceRegistry_;
}

pxr::HdRenderParam *HdFluxRenderDelegate::GetRenderParam() const { return renderParam_.get(); }

pxr::HdRenderPassSharedPtr HdFluxRenderDelegate::CreateRenderPass(
    pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection
) {
    return std::make_shared<HdFluxRenderPass>(index, collection);
}

pxr::HdInstancer *
HdFluxRenderDelegate::CreateInstancer(pxr::HdSceneDelegate *, pxr::SdfPath const &) {
    return nullptr;
}

void HdFluxRenderDelegate::DestroyInstancer(pxr::HdInstancer *instancer) { delete instancer; }

pxr::HdRprim *HdFluxRenderDelegate::CreateRprim(pxr::TfToken const &, pxr::SdfPath const &) {
    return nullptr;
}

void HdFluxRenderDelegate::DestroyRprim(pxr::HdRprim *rprim) { delete rprim; }

pxr::HdSprim *
HdFluxRenderDelegate::CreateSprim(pxr::TfToken const &typeId, pxr::SdfPath const &sprimId) {
    if (typeId == pxr::HdPrimTypeTokens->camera)
        return new pxr::HdCamera(sprimId);
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
    renderParam_->GetHandler().sync();
} catch (...) { renderParam_->ReportFailure(); }

pxr::HdAovDescriptor HdFluxRenderDelegate::GetDefaultAovDescriptor(pxr::TfToken const &name) const {
    return HdFluxGetAovDescriptor(name);
}

pxr::HdRenderSettingDescriptorList HdFluxRenderDelegate::GetRenderSettingDescriptors() const {
    return {
        {"Converged Samples Per Pixel", pxr::HdRenderSettingsTokens->convergedSamplesPerPixel,
         pxr::VtValue(HdFluxDefaultConvergedSamplesPerPixel)},
    };
}
