#pragma once

#include <filesystem>

/// \brief Returns the folder of the hdFlux plugin that holds the MaterialX \c libraries folder.
///
/// \pre Plug has registered the plugin.
[[nodiscard]] std::filesystem::path HdFluxGetMaterialXRoot();
