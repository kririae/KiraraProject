#include "flux/Shading/Mtlx/MtlxEmission.h"

#include <MaterialXCore/Types.h>
#include <MaterialXGenShader/ShaderNode.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

#include "flux/Shading/Mtlx/FLuxShaderGraphRefactor.h"

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

[[nodiscard]] std::optional<ZeroProof>
findZeroProof(MaterialX::ShaderGraph const &graph, MaterialX::ShaderNode const &node);

/// \brief A proof that the value on \p input is zero, or \c std::nullopt if none is found.
[[nodiscard]] std::optional<ZeroProof>
findZeroProof(MaterialX::ShaderGraph const &graph, MaterialX::ShaderInput const &input) {
    auto const *source = input.getConnection();
    if (source && !source->getNode()->isAGraph())
        return findZeroProof(graph, *source->getNode());
    if (!source && input.getType() == MaterialX::Type::EDF)
        return ZeroProof{};

    auto const value = source ? source->getValue() : input.getValue();
    if (!value || !isZero(*value))
        return std::nullopt;
    return ZeroProof{source ? source->getPath() : input.getPath()};
}

/// \brief A proof that the value of \p node is zero, or \c std::nullopt if none is found.
[[nodiscard]] std::optional<ZeroProof>
findZeroProof(MaterialX::ShaderGraph const &graph, MaterialX::ShaderNode const &node) {
    auto const category = getDocumentNodeCategory(graph, node);
    auto const allZero = [&](std::initializer_list<char const *> names) {
        auto proof = std::optional<ZeroProof>{std::in_place};
        for (auto const *name : names) {
            auto const part = findZeroProof(graph, *node.getInput(name));
            if (!part)
                return std::optional<ZeroProof>{};
            proof->insert(part->begin(), part->end());
        }
        return proof;
    };

    if (category == "constant")
        return allZero({"value"});
    if (category == "convert")
        return allZero({"in"});
    if (category == "uniform_edf")
        return allZero({"color"});
    if (category == "conical_edf" || category == "measured_edf")
        return ZeroProof{};
    if (category == "add" || category == "subtract")
        return allZero({"in1", "in2"});
    if (category == "multiply") {
        auto proof = allZero({"in1"});
        return proof ? proof : allZero({"in2"});
    }
    if (category == "divide")
        return allZero({"in1"});
    if (category == "mix")
        return allZero({"fg", "bg"});
    if (category == "generalized_schlick_edf") {
        auto proof = allZero({"base"});
        return proof ? proof : allZero({"color0", "color90"});
    }
    return std::nullopt;
}
} // namespace

std::optional<ZeroProof> findZeroProof(MaterialX::ShaderGraph const &graph) {
    auto const surface =
        std::ranges::find_if(graph.getNodes(), [](MaterialX::ShaderNode const *node) {
        return node->hasClassification(MaterialX::ShaderNode::Classification::SURFACE);
    });
    if (surface == graph.getNodes().end())
        return ZeroProof{};
    return findZeroProof(graph, *(*surface)->getInput("edf"));
}
} // namespace flux
