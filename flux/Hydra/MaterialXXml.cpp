#include "flux/Hydra/MaterialXXml.h"

#include <MaterialXFormat/XmlIo.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdMtlx/hdMtlx.h>
#include <pxr/usd/sdf/assetPath.h>

#include "flux/Hydra/ResolvedPath.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace mx = MaterialX;

namespace {
HdMaterialNetwork2 resolveAssetPaths(HdMaterialNetwork2 network, SdfPath const &materialPath) {
    for (auto &[path, node] : network.nodes) {
        for (auto &[name, value] : node.parameters) {
            if (!value.IsHolding<SdfAssetPath>())
                continue;

            value = SdfAssetPath{
                HdFLuxGetResolvedPath(value.UncheckedGet<SdfAssetPath>(), materialPath)
            };
        }
    }
    return network;
}
} // namespace

std::optional<HdFLuxMaterialXXml>
HdFLuxToMaterialXXml(HdMaterialNetwork2 const &network, SdfPath const &materialPath) {
    auto const terminal = network.terminals.find(HdMaterialTerminalTokens->surface);
    if (terminal == network.terminals.end())
        return std::nullopt;

    auto const &terminalPath = terminal->second.upstreamNode;
    auto const resolved = resolveAssetPaths(network, materialPath);
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
    return HdFLuxMaterialXXml{
        mx::writeToXmlString(document, &options), surfaceMaterials.front()->getName()
    };
}
