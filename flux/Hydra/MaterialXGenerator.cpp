#include "flux/Hydra/MaterialXGenerator.h"

#include <utility>

#include "flux/Mtlx/MtlxBSDFGenerator.h"

struct HdFluxMaterialXGenerator::Impl {
    explicit Impl(std::filesystem::path const &mtlxRoot) : generator{mtlxRoot} {}

    flux::MtlxBSDFGenerator generator;
};

HdFluxMaterialXGenerator::HdFluxMaterialXGenerator(std::filesystem::path const &mtlxRoot)
    : impl_{std::make_unique<Impl>(mtlxRoot)} {}

HdFluxMaterialXGenerator::~HdFluxMaterialXGenerator() = default;

std::optional<HdFluxMaterialXGenerator::Instance> HdFluxMaterialXGenerator::generate(
    std::string const &xml, std::string const &surfaceMaterialName
) const {
    auto result = impl_->generator.generate(xml, surfaceMaterialName, {});
    if (!result.instance)
        return std::nullopt;

    auto &instance = *result.instance;
    return Instance{
        .spec = std::move(instance.spec),
        .words = std::move(instance.words),
        .textures = std::move(instance.textures)
    };
}
