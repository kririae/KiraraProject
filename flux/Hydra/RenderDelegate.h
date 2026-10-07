#pragma once

#include <pxr/imaging/hd/renderDelegate.h>

/// \brief The FLux Hydra render delegate.
class HdFluxRenderDelegate final : public pxr::HdRenderDelegate {
public:
    HdFluxRenderDelegate();
    explicit HdFluxRenderDelegate(pxr::HdRenderSettingsMap const &settingsMap);

    HdFluxRenderDelegate(HdFluxRenderDelegate const &) = delete;
    HdFluxRenderDelegate &operator=(HdFluxRenderDelegate const &) = delete;

    pxr::TfTokenVector const &GetSupportedRprimTypes() const override;
    pxr::TfTokenVector const &GetSupportedSprimTypes() const override;
    pxr::TfTokenVector const &GetSupportedBprimTypes() const override;

    pxr::HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    pxr::HdRenderPassSharedPtr
    CreateRenderPass(pxr::HdRenderIndex *index, pxr::HdRprimCollection const &collection) override;

    pxr::HdInstancer *
    CreateInstancer(pxr::HdSceneDelegate *delegate, pxr::SdfPath const &id) override;
    void DestroyInstancer(pxr::HdInstancer *instancer) override;

    pxr::HdRprim *CreateRprim(pxr::TfToken const &typeId, pxr::SdfPath const &rprimId) override;
    void DestroyRprim(pxr::HdRprim *rprim) override;

    pxr::HdSprim *CreateSprim(pxr::TfToken const &typeId, pxr::SdfPath const &sprimId) override;
    pxr::HdSprim *CreateFallbackSprim(pxr::TfToken const &typeId) override;
    void DestroySprim(pxr::HdSprim *sprim) override;

    pxr::HdBprim *CreateBprim(pxr::TfToken const &typeId, pxr::SdfPath const &bprimId) override;
    pxr::HdBprim *CreateFallbackBprim(pxr::TfToken const &typeId) override;
    void DestroyBprim(pxr::HdBprim *bprim) override;

    void CommitResources(pxr::HdChangeTracker *tracker) override;

    pxr::HdAovDescriptor GetDefaultAovDescriptor(pxr::TfToken const &name) const override;

private:
    pxr::HdResourceRegistrySharedPtr resourceRegistry_;
};
