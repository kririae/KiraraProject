#include "flux/Hydra/MaterialXXml.h"

#include <MaterialXFormat/XmlIo.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hdMtlx/hdMtlx.h>
#include <pxr/usd/sdf/assetPath.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace mx = MaterialX;

namespace {
HdMaterialNetwork2 withResolvedAssetPaths(HdMaterialNetwork2 network) {
    for (auto &[path, node] : network.nodes) {
        for (auto &[name, value] : node.parameters) {
            if (!value.IsHolding<SdfAssetPath>())
                continue;

            auto const &resolved = value.UncheckedGet<SdfAssetPath>().GetResolvedPath();
            if (!resolved.empty())
                value = SdfAssetPath{resolved};
        }
    }
    return network;
}
} // namespace

std::optional<HdFluxMaterialXXml> HdFluxCreateMaterialXXml(
    HdMaterialNetwork2 const &network, SdfPath const &terminalPath, SdfPath const &materialPath
) {
    auto const resolved = withResolvedAssetPaths(network);
    auto const document = HdMtlxCreateMtlxDocumentFromHdNetwork(
        resolved, resolved.nodes.at(terminalPath), terminalPath, materialPath, HdMtlxStdLibraries()
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
    return HdFluxMaterialXXml{
        mx::writeToXmlString(document, &options), surfaceMaterials.front()->getName()
    };
}
