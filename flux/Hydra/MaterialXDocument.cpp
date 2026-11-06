#include "flux/Hydra/MaterialXDocument.h"

#include <MaterialXFormat/XmlIo.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hdMtlx/hdMtlx.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace mx = MaterialX;

std::optional<HdFluxMaterialXDocument> HdFluxCreateMaterialXDocument(
    HdMaterialNetwork2 const &network, SdfPath const &terminalPath, SdfPath const &materialPath
) {
    auto const document = HdMtlxCreateMtlxDocumentFromHdNetwork(
        network, network.nodes.at(terminalPath), terminalPath, materialPath, HdMtlxStdLibraries()
    );
    if (!document)
        return std::nullopt;

    auto const surfaceMaterials = document->getMaterialNodes();
    if (surfaceMaterials.empty())
        return std::nullopt;

    mx::XmlWriteOptions options;
    options.writeXIncludeEnable = false;
    options.elementPredicate = [](mx::ConstElementPtr const &element) {
        return !element->hasSourceUri();
    };
    return HdFluxMaterialXDocument{
        mx::writeToXmlString(document, &options), surfaceMaterials.front()->getName()
    };
}
