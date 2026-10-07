#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

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
    pxr::HdRenderDelegate *CreateRenderDelegate() override { return new HdFluxRenderDelegate(); }

    pxr::HdRenderDelegate *
    CreateRenderDelegate(pxr::HdRenderSettingsMap const &settingsMap) override {
        return new HdFluxRenderDelegate(settingsMap);
    }

    void DeleteRenderDelegate(pxr::HdRenderDelegate *renderDelegate) override {
        delete renderDelegate;
    }

    bool IsSupported(bool) const override { return true; }
};

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType) { HdRendererPluginRegistry::Define<HdFluxRendererPlugin>(); }
