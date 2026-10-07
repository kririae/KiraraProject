#include "flux/Hydra/RenderDelegate.h"

#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sprim.h>

#include <memory>

namespace {
class HdFluxRenderPass final : public pxr::HdRenderPass {
public:
    using pxr::HdRenderPass::HdRenderPass;

private:
    void _Execute(pxr::HdRenderPassStateSharedPtr const &, pxr::TfTokenVector const &) override {}
};
} // namespace

HdFluxRenderDelegate::HdFluxRenderDelegate()
    : resourceRegistry_(std::make_shared<pxr::HdResourceRegistry>()) {}

HdFluxRenderDelegate::HdFluxRenderDelegate(pxr::HdRenderSettingsMap const &settingsMap)
    : pxr::HdRenderDelegate(settingsMap),
      resourceRegistry_(std::make_shared<pxr::HdResourceRegistry>()) {}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedRprimTypes() const {
    static auto const types = pxr::TfTokenVector{};
    return types;
}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedSprimTypes() const {
    static auto const types = pxr::TfTokenVector{};
    return types;
}

pxr::TfTokenVector const &HdFluxRenderDelegate::GetSupportedBprimTypes() const {
    static auto const types = pxr::TfTokenVector{};
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

pxr::HdSprim *HdFluxRenderDelegate::CreateSprim(pxr::TfToken const &, pxr::SdfPath const &) {
    return nullptr;
}

pxr::HdSprim *HdFluxRenderDelegate::CreateFallbackSprim(pxr::TfToken const &) { return nullptr; }

void HdFluxRenderDelegate::DestroySprim(pxr::HdSprim *sprim) { delete sprim; }

pxr::HdBprim *HdFluxRenderDelegate::CreateBprim(pxr::TfToken const &, pxr::SdfPath const &) {
    return nullptr;
}

pxr::HdBprim *HdFluxRenderDelegate::CreateFallbackBprim(pxr::TfToken const &) { return nullptr; }

void HdFluxRenderDelegate::DestroyBprim(pxr::HdBprim *bprim) { delete bprim; }

void HdFluxRenderDelegate::CommitResources(pxr::HdChangeTracker *) {}
