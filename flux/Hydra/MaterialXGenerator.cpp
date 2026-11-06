#include "flux/Hydra/MaterialXGenerator.h"

#include <MaterialXFormat/File.h>
#include <MaterialXFormat/XmlIo.h>

#include <utility>

#include "flux/Mtlx/MtlxBSDFGenerator.h"

namespace mx = MaterialX;

struct HdFluxMaterialXGenerator::Impl {
    flux::MtlxBSDFGenerator generator;
};

HdFluxMaterialXGenerator::HdFluxMaterialXGenerator(std::filesystem::path const &mtlxRoot)
    : impl_{std::make_unique<Impl>(flux::MtlxBSDFGenerator{mtlxRoot})} {}

HdFluxMaterialXGenerator::~HdFluxMaterialXGenerator() = default;

std::optional<HdFluxMaterialXGenerator::Material> HdFluxMaterialXGenerator::generate(
    std::string const &xml, std::string const &surfaceMaterialName
) const {
    auto const document = mx::createDocument();
    try {
        mx::readFromXmlString(document, xml);
    } catch (mx::Exception const &) { return std::nullopt; }

    auto result = impl_->generator.generate(document, surfaceMaterialName, mx::FileSearchPath{});
    if (!result.instance)
        return std::nullopt;

    auto &instance = *result.instance;
    return Material{
        .spec = std::move(instance.spec),
        .words = std::move(instance.words),
        .textures = std::move(instance.textures)
    };
}
