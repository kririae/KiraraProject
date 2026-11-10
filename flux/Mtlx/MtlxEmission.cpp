#include "flux/Mtlx/MtlxEmission.h"

#include <MaterialXCore/Types.h>
#include <MaterialXGenShader/Exception.h>
#include <MaterialXGenShader/ShaderNode.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "flux/Mtlx/FluxShaderGraphRefactor.h"

namespace flux {
namespace {
template <class Vector> [[nodiscard]] bool isZeroVector(Vector const &vector) {
    return std::all_of(vector.data(), vector.data() + Vector::numElements(), [](float channel) {
        return channel == 0.0F;
    });
}

/// \brief Whether a literal is zero in every channel.
[[nodiscard]] bool isZero(MaterialX::Value const &value) {
    if (value.isA<float>())
        return value.asA<float>() == 0.0F;
    if (value.isA<MaterialX::Color3>())
        return isZeroVector(value.asA<MaterialX::Color3>());
    if (value.isA<MaterialX::Vector3>())
        return isZeroVector(value.asA<MaterialX::Vector3>());
    return false;
}

[[nodiscard]] bool isKnownZero(
    MaterialX::ShaderGraph const &graph, InteractiveInputs const &interactive,
    MaterialX::ShaderNode const &node
);

/// \brief Whether the value on \p input is provably zero.
[[nodiscard]] bool isKnownZero(
    MaterialX::ShaderGraph const &graph, InteractiveInputs const &interactive,
    MaterialX::ShaderInput const &input
) {
    auto const *source = input.getConnection();
    if (source && !source->getNode()->isAGraph())
        return isKnownZero(graph, interactive, *source->getNode());
    if (!source && input.getType() == MaterialX::Type::EDF)
        return true;

    auto const &path = source ? source->getPath() : input.getPath();
    auto const value = source ? source->getValue() : input.getValue();
    return value && !interactive.contains(path) && isZero(*value);
}

/// \brief Whether the value of \p node is provably zero.
[[nodiscard]] bool isKnownZero(
    MaterialX::ShaderGraph const &graph, InteractiveInputs const &interactive,
    MaterialX::ShaderNode const &node
) {
    auto const category = getDocumentNodeCategory(graph, node);
    auto const zero = [&](std::string const &name) {
        return isKnownZero(graph, interactive, *node.getInput(name));
    };

    if (category == "constant")
        return zero("value");
    if (category == "convert")
        return zero("in");
    if (category == "uniform_edf")
        return zero("color");
    if (category == "conical_edf" || category == "measured_edf")
        return true;
    if (category == "add" || category == "subtract")
        return zero("in1") && zero("in2");
    if (category == "multiply")
        return zero("in1") || zero("in2");
    if (category == "divide")
        return zero("in1");
    if (category == "mix")
        return zero("fg") && zero("bg");
    if (category == "generalized_schlick_edf")
        return zero("base") || (zero("color0") && zero("color90"));
    return false;
}

/// \brief The implementations of the nodes that read the tangent of the shading context.
constexpr auto tangentImplementations =
    std::to_array<std::string_view>({"IM_tangent_vector3_genflux", "IM_bitangent_vector3_genflux"});

/// \brief Returns the surface node of \p graph, or null if it has none.
[[nodiscard]] MaterialX::ShaderNode const *findSurface(MaterialX::ShaderGraph const &graph) {
    auto const surface =
        std::ranges::find_if(graph.getNodes(), [](MaterialX::ShaderNode const *node) {
        return node->hasClassification(MaterialX::ShaderNode::Classification::SURFACE);
    });
    return surface == graph.getNodes().end() ? nullptr : *surface;
}
} // namespace

bool mayEmit(MaterialX::ShaderGraph const &graph, InteractiveInputs const &interactive) {
    auto const *surface = findSurface(graph);
    return surface && !isKnownZero(graph, interactive, *surface->getInput("edf"));
}

void requireEmissionWithoutTangent(
    MaterialX::ShaderGraph const &graph, InteractiveInputs const &interactive
) {
    if (!mayEmit(graph, interactive))
        return;

    auto visited = std::set<MaterialX::ShaderNode const *>{};
    auto pending = std::vector<MaterialX::ShaderNode const *>{};
    auto const visitSource = [&](MaterialX::ShaderInput const &input) {
        auto const *source = input.getConnection();
        if (source && !source->getNode()->isAGraph() && visited.insert(source->getNode()).second)
            pending.push_back(source->getNode());
    };
    visitSource(*findSurface(graph)->getInput("edf"));
    while (!pending.empty()) {
        auto const *node = pending.back();
        pending.pop_back();
        if (std::ranges::find(tangentImplementations, node->getImplementation().getName()) !=
            tangentImplementations.end())
            throw MaterialX::ExceptionShaderGenError{
                fmt::format("'{}' reads a tangent, which an emission cannot read", node->getName())
            };
        for (auto const *input : node->getInputs())
            visitSource(*input);
    }
}
} // namespace flux
