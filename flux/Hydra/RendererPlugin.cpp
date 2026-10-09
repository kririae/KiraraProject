#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include <exception>
#include <filesystem>

#include "flux/Core/Logging.h"
#include "flux/Hydra/RenderDelegate.h"

// Without NDEBUG, Houdini's pxr.h defines _DEBUG and TBB_USE_DEBUG, which do not match the release
// USD and TBB that Houdini ships.
#ifndef NDEBUG
#error "The Hydra delegate does not support Debug builds"
#endif

/// \brief Creates FLux render delegates.
///
/// Outside any namespace: Houdini keys renderer configuration by the type name.
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

    bool IsSupported(bool) const override { return true; }

private:
    /// \brief Returns a new delegate. If it cannot start, logs the error and returns null.
    static pxr::HdRenderDelegate *createDelegate(pxr::HdRenderSettingsMap const &settingsMap) {
        try {
            return new HdFluxRenderDelegate(getModulePath(), settingsMap);
        } catch (std::exception const &error) {
            flux::LogError(
                "HdFluxRendererPlugin: cannot create a render delegate: {}", error.what()
            );
            return nullptr;
        }
    }

    /// \brief Returns the path of the OptiX IR module, which is placed next to the plugin
    /// library.
    static std::filesystem::path getModulePath() {
        // Plug loaded this library, so it knows the plugin and the path it loaded from.
        auto const plugin = pxr::PlugRegistry::GetInstance().GetPluginForType(
            pxr::TfType::Find<HdFluxRendererPlugin>()
        );
        return std::filesystem::path{plugin->GetPath()}.parent_path() / FLUX_OPTIX_IR_NAME;
    }
};

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType) { HdRendererPluginRegistry::Define<HdFluxRendererPlugin>(); }
