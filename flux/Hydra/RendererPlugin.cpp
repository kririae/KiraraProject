#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/pxr.h>

#include <exception>
#include <filesystem>
#include <string>

#include "flux/Core/Logging.h"
#include "flux/Hydra/RenderDelegate.h"

#ifndef NDEBUG
#error "The Hydra delegate does not support Debug builds"
#endif

/// \brief Creates FLux render delegates.
class HdFluxRendererPlugin final : public pxr::HdRendererPlugin {
public:
    pxr::HdRenderDelegate *CreateRenderDelegate() override { return createDelegate({}); }

    pxr::HdRenderDelegate *
    CreateRenderDelegate(pxr::HdRenderSettingsMap const &settingsMap) override {
        return createDelegate(settingsMap);
    }

    void DeleteRenderDelegate(pxr::HdRenderDelegate *renderDelegate) override {
        delete renderDelegate;
    }

#if PXR_VERSION >= 2605
    bool IsSupported(pxr::HdRendererCreateArgs const &, std::string *) const override {
        return true;
    }
#else
    bool IsSupported(bool) const override { return true; }
#endif

private:
    /// \brief Returns a new delegate. If it cannot start, logs the error and returns null.
    static pxr::HdRenderDelegate *createDelegate(pxr::HdRenderSettingsMap const &settingsMap) {
        try {
            auto const plugin = pxr::PlugRegistry::GetInstance().GetPluginForType(
                pxr::TfType::Find<HdFluxRendererPlugin>()
            );
            auto const mtlxRoot = std::filesystem::path{plugin->GetResourcePath()};
            return new HdFluxRenderDelegate(
                std::filesystem::path{plugin->GetPath()}.parent_path() / FLUX_OPTIX_IR_NAME,
                mtlxRoot, settingsMap
            );
        } catch (std::exception const &error) {
            flux::LogError(
                "HdFluxRendererPlugin: cannot create a render delegate: {}", error.what()
            );
            return nullptr;
        }
    }
};

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType) { HdRendererPluginRegistry::Define<HdFluxRendererPlugin>(); }
