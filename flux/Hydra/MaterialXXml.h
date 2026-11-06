#pragma once

#include <pxr/usd/sdf/path.h>

#include <optional>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE
struct HdMaterialNetwork2;
PXR_NAMESPACE_CLOSE_SCOPE

/// \brief The XML of one MaterialX material, with the name of its surface material.
struct HdFluxMaterialXXml {
    /// The XML of the elements that the material owns, with none of the standard library.
    std::string xml;
    std::string surfaceMaterialName;
};

/// \pre \p terminalPath is the path of a node of \p network.
/// \pre The environment variable \c PXR_MTLX_STDLIB_SEARCH_PATHS names the standard libraries of
/// MaterialX, as the host sets it.
std::optional<HdFluxMaterialXXml> HdFluxCreateMaterialXXml(
    PXR_NS::HdMaterialNetwork2 const &network, PXR_NS::SdfPath const &terminalPath,
    PXR_NS::SdfPath const &materialPath
);
