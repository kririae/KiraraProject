#include "flux/Hydra/RenderDelegate.h"

#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sprim.h>
#include <pxr/imaging/hd/tokens.h>

#include <memory>

#include "flux/Hydra/RenderBuffer.h"
#include "flux/Hydra/RenderPass.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Context.h"
#include "kira/Properties.h"

namespace {
flux::Ref<flux::Context> makeContext() {
    auto context = flux::Context::create();
    context->setActiveIntegrator(context->create<flux::PathIntegrator>(kira::Properties{}));
    context->setActiveSampler(context->create<flux::IndependentSampler>(kira::Properties{}));
    return context;
}
} // namespace

HdFluxRenderDelegate::HdFluxRenderDelegate(
    std::filesystem::path const &modulePath, pxr::HdRenderSettingsMap const &settingsMap
)
    : pxr::HdRenderDelegate(settingsMap),
      resourceRegistry_(std::make_shared<pxr::HdResourceRegistry>()), context_(makeContext()),
      handler_(context_, modulePath) {}

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

void HdFluxRenderDelegate::CommitResources(pxr::HdChangeTracker *) {}

pxr::HdAovDescriptor HdFluxRenderDelegate::GetDefaultAovDescriptor(pxr::TfToken const &name) const {
    // The renderer resolves its samples itself, so the buffer is not multisampled.
    if (name == pxr::HdAovTokens->color)
        return {pxr::HdFormatFloat32Vec4, false, pxr::VtValue(pxr::GfVec4f(0.0F))};
    return {};
}
