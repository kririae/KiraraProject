#pragma once

#include <pxr/imaging/hd/renderDelegate.h>

#include <filesystem>
#include <memory>

class HdFluxRenderParam;

/// \brief The interface to configure the FLux's context from Hydra.
class HdFluxRenderDelegate final : public pxr::HdRenderDelegate {
public:
    /// \brief Creates a delegate that renders with the OptiX IR module at \p modulePath.
    ///
    /// \throw std::exception If the OptiX backend cannot start.
    HdFluxRenderDelegate(
        std::filesystem::path const &modulePath, pxr::HdRenderSettingsMap const &settingsMap
    );
    ~HdFluxRenderDelegate() override;

    HdFluxRenderDelegate(HdFluxRenderDelegate const &) = delete;
    HdFluxRenderDelegate &operator=(HdFluxRenderDelegate const &) = delete;

    pxr::TfTokenVector const &GetSupportedRprimTypes() const override;
    pxr::TfTokenVector const &GetSupportedSprimTypes() const override;
    pxr::TfTokenVector const &GetSupportedBprimTypes() const override;

    pxr::TfToken GetMaterialBindingPurpose() const override;
    pxr::TfTokenVector GetMaterialRenderContexts() const override;

    pxr::HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    pxr::HdRenderParam *GetRenderParam() const override;

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

    pxr::HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

    /// \brief Returns the render statistics.
    ///
    /// After a failure, they hold \c huskErrorStatus 1.
    pxr::VtDictionary GetRenderStats() const override;

private:
    pxr::HdResourceRegistrySharedPtr resourceRegistry_;
    std::unique_ptr<HdFluxRenderParam> renderParam_;
};
