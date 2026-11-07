#include "flux/Mtlx/FluxShaderGraphRefactor.h"

#include <MaterialXGenShader/ShaderNode.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace flux {
namespace {
using Classification = MaterialX::ShaderNode::Classification;

constexpr auto bsdfClassification = Classification::BSDF | Classification::CLOSURE;

/// The value of MaterialX's \c M_FLOAT_EPS.
constexpr auto weightEpsilon = 1e-8F;

/// The categories of the BSDFs whose result at a weight below \c weightEpsilon is the empty BSDF.
constexpr auto emptyAtZeroWeight =
    std::to_array<std::string_view>({"dielectric_bsdf", "generalized_schlick_bsdf", "sheen_bsdf"});

/// \brief The float that the document writes at \p input, if it is unconnected and not interactive.
[[nodiscard]] std::optional<float> getLiteral(
    MaterialX::ShaderNode const &node, std::string const &name, MaterialX::GenContext &context
) {
    auto const *input = node.getInput(name);
    if (!input || input->getConnection() || !input->getValue() || !input->getValue()->isA<float>())
        return std::nullopt;
    auto const interactive = context.getUserData<InteractiveInputs>(InteractiveInputs::name);
    if (interactive && interactive->contains(input->getPath()))
        return std::nullopt;
    return input->getValue()->asA<float>();
}

/// \brief Reconnects the consumers of \p node to what its input \p name reads.
void bypassInput(
    MaterialX::ShaderGraph &graph, MaterialX::ShaderNode &node, std::string const &name
) {
    auto const &inputs = node.getInputs();
    graph.bypass(&node, std::ranges::find(inputs, node.getInput(name)) - inputs.begin());
}

/// \brief The category of the document node that \p node comes from, or an empty string.
[[nodiscard]] std::string
getCategory(MaterialX::ShaderGraph const &graph, MaterialX::ShaderNode const &node) {
    auto const element = graph.getDocument()->getDescendant(node.getUniqueId());
    return element ? element->getCategory() : std::string{};
}

[[nodiscard]] bool isConsumed(MaterialX::ShaderNode const &node) {
    return !node.getOutput()->getConnections().empty();
}
} // namespace

std::string const &LiteralMixElisionRefactor::getName() const {
    static auto const name = std::string{"literalMixElision"};
    return name;
}

std::size_t
LiteralMixElisionRefactor::execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) {
    auto edits = std::size_t{};
    for (auto *node : graph.getNodes()) {
        if (!node->hasClassification(bsdfClassification | Classification::MIX) ||
            !isConsumed(*node))
            continue;
        auto const mix = getLiteral(*node, "mix", context);
        if (mix != 0.0F && mix != 1.0F)
            continue;
        bypassInput(graph, *node, mix == 1.0F ? "fg" : "bg");
        ++edits;
    }
    return edits;
}

std::string const &ZeroWeightBsdfElisionRefactor::getName() const {
    static auto const name = std::string{"zeroWeightBsdfElision"};
    return name;
}

std::size_t ZeroWeightBsdfElisionRefactor::execute(
    MaterialX::ShaderGraph &graph, MaterialX::GenContext &context
) {
    auto edits = std::size_t{};
    for (auto *node : graph.getNodes()) {
        if (!isConsumed(*node) || std::ranges::find(emptyAtZeroWeight, getCategory(graph, *node)) ==
                                      emptyAtZeroWeight.end())
            continue;
        auto const weight = getLiteral(*node, "weight", context);
        if (!weight || *weight >= weightEpsilon)
            continue;
        node->getOutput()->breakConnections();
        ++edits;
    }
    return edits;
}

std::string const &EmptyTopLayerElisionRefactor::getName() const {
    static auto const name = std::string{"emptyTopLayerElision"};
    return name;
}

std::size_t
EmptyTopLayerElisionRefactor::execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &) {
    auto edits = std::size_t{};
    for (auto *node : graph.getNodes()) {
        if (!node->hasClassification(bsdfClassification | Classification::LAYER) ||
            !isConsumed(*node) || node->getInput("top")->getConnection())
            continue;
        bypassInput(graph, *node, "base");
        ++edits;
    }
    return edits;
}
} // namespace flux
