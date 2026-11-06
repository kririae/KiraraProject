#include "flux/Hydra/MaterialXRoot.h"

#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

std::filesystem::path HdFluxGetMaterialXRoot() {
    auto const plugin = pxr::PlugRegistry::GetInstance().GetPluginWithName("hdFlux");
    return plugin->GetResourcePath();
}
