#pragma once

#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>

#include <filesystem>
#include <memory>
#include <optional>

#include "flux/Hydra/SampleSchedule.h"
#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"

class HdFLuxRenderer;
class HdFLuxRenderParam;

namespace flux {
class Context;
} // namespace flux

/// \brief The interface to configure the FLux's context from Hydra.
///
/// The prims edit the context while the render thread renders the scene of the last sync.
/// Committing the resources translates the render settings, and stops the render thread before it
/// syncs the scene.
class HdFLuxRenderDelegate final : public pxr::HdRenderDelegate {
public:
    /// \param mtlxRoot The folder that holds the MaterialX \c libraries folder.
    /// \throw std::exception If the OptiX backend or the shader generator cannot start.
    HdFLuxRenderDelegate(
        std::filesystem::path const &mtlxRoot, pxr::HdRenderSettingsMap const &settingsMap
    );
    ~HdFLuxRenderDelegate() override;

    HdFLuxRenderDelegate(HdFLuxRenderDelegate const &) = delete;
    HdFLuxRenderDelegate &operator=(HdFLuxRenderDelegate const &) = delete;

public:
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

public:
    void CommitResources(pxr::HdChangeTracker *tracker) override;

    bool IsPauseSupported() const override;
    bool Pause() override;
    bool Resume() override;

    pxr::HdAovDescriptor GetDefaultAovDescriptor(pxr::TfToken const &name) const override;

    pxr::HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

    /// \brief Returns the render statistics.
    ///
    /// After a failure, they hold \c huskErrorStatus 1.
    pxr::VtDictionary GetRenderStats() const override;

    /// \brief Returns the sample schedule that the render settings ask for, as of the last commit
    /// of the resources.
    [[nodiscard]] HdFLuxSampleSchedule const &GetSchedule() const noexcept { return schedule_; }

private:
    /// \brief Makes the active integrator of \p context follow the render settings.
    void syncIntegrator(flux::Context &context) const;

    /// \brief Makes the active sampler of \p context follow the render settings.
    void syncSampler(flux::Context &context) const;

    pxr::HdResourceRegistrySharedPtr resourceRegistry_;

    pxr::HdRenderThread renderThread_;
    std::unique_ptr<HdFLuxRenderParam> renderParam_;
    std::unique_ptr<HdFLuxRenderer> renderer_;

    std::optional<unsigned int> settingsVersion_;
    HdFLuxSampleSchedule schedule_;
    flux::PathIntegrator::Impl defaultIntegrator_;
    bool defaultShaderReorder_;
    flux::SamplerType defaultSampler_;
};
