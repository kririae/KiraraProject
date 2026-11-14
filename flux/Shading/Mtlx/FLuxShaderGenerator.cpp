#include "flux/Shading/Mtlx/FLuxShaderGenerator.h"

#include <MaterialXCore/Definition.h>
#include <MaterialXGenShader/Exception.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/ShaderStage.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "flux/Shading/Mtlx/FLuxShaderGraphRefactor.h"
#include "flux/Shading/Mtlx/FLuxSourceCodeNode.h"
#include "flux/Shading/Mtlx/FLuxSyntax.h"

namespace flux {
namespace {
/// \brief The nodegroups whose nodes read the state of a rasterization pipeline.
constexpr auto pipelineNodeGroups = std::to_array<std::string_view>(
    {"application", "convolution2d", "geometric", "texture2d", "texture3d"}
);

/// \brief The categories outside \c pipelineNodeGroups whose nodes read the state of a
/// rasterization pipeline.
constexpr auto pipelineNodeCategories = std::to_array<std::string_view>(
    {"transformnormal", "transformpoint", "transformvector", "viewdirection"}
);

[[nodiscard]] bool
hasClosureOutput(MaterialX::NodeDef const &nodeDef, MaterialX::TypeSystem const &types) {
    auto const isClosure = [&](std::string const &type) { return types.getType(type).isClosure(); };

    auto const outputs = nodeDef.getActiveOutputs();
    if (outputs.empty())
        return isClosure(nodeDef.getType());
    return std::ranges::any_of(outputs, [&](MaterialX::OutputPtr const &output) {
        return isClosure(output->getType());
    });
}

[[nodiscard]] bool readsPipeline(MaterialX::NodeDef const &nodeDef) {
    auto const isNamed = [](auto const &names, std::string const &name) {
        return std::ranges::find(names, name) != names.end();
    };
    return isNamed(pipelineNodeGroups, nodeDef.getNodeGroup()) ||
           isNamed(pipelineNodeCategories, nodeDef.getNodeString());
}
} // namespace

FLuxShaderGenerator::FLuxShaderGenerator(MaterialX::TypeSystemPtr typeSystem)
    : SlangShaderGenerator{typeSystem} {
    _syntax = std::make_shared<FLuxSyntax>(std::move(typeSystem));
    registerRefactor(std::make_shared<LiteralMixElisionRefactor>());
    registerRefactor(std::make_shared<ZeroWeightBsdfElisionRefactor>());
    registerRefactor(std::make_shared<EmptyTopLayerElisionRefactor>());
}

MaterialX::ShaderStagePtr FLuxShaderGenerator::makeStage() const {
    return std::make_shared<MaterialX::ShaderStage>(MaterialX::Stage::PIXEL, _syntax);
}

void FLuxShaderGenerator::substituteTokens(MaterialX::ShaderStage &stage) const {
    replaceTokens(_tokenSubstitutions, stage);
    SlangSyntaxFromGlsl(stage);
}

std::string const &FLuxShaderGenerator::getTarget() const {
    static auto const Target = std::string{"genflux"};
    return Target;
}

MaterialX::ShaderNodeImplPtr FLuxShaderGenerator::getImplementation(
    MaterialX::NodeDef const &nodeDef, MaterialX::GenContext &context
) const {
    auto const implementation = nodeDef.getImplementation(getTarget());
    auto const implementedHere = implementation && implementation->getTarget() == getTarget();
    if (!implementedHere && (hasClosureOutput(nodeDef, *getTypeSystem()) || readsPipeline(nodeDef)))
        throw MaterialX::ExceptionShaderGenError{
            fmt::format("node '{}' has no genflux implementation", nodeDef.getNodeString())
        };
    return SlangShaderGenerator::getImplementation(nodeDef, context);
}

MaterialX::ShaderNodeImplPtr FLuxShaderGenerator::createShaderNodeImplForImplementation(
    MaterialX::Implementation const &
) const {
    return FLuxSourceCodeNode::create();
}
} // namespace flux
