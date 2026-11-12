#pragma once

#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/ShaderGraph.h>
#include <MaterialXGenShader/ShaderGraphRefactor.h>
#include <MaterialXGenShader/ShaderNode.h>

#include <cstddef>
#include <string>

namespace flux {
/// \brief The category of the document node that \p node comes from, or an empty string.
[[nodiscard]] std::string
getDocumentNodeCategory(MaterialX::ShaderGraph const &graph, MaterialX::ShaderNode const &node);

/// \brief Replaces a BSDF mix whose literal mix is 0 or 1 by the BSDF that it selects.
class LiteralMixElisionRefactor final : public MaterialX::ShaderGraphRefactor {
public:
    [[nodiscard]] std::string const &getName() const override;
    std::size_t execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) override;
};

/// \brief Removes a BSDF that a literal weight below \c M_FLOAT_EPS makes empty, so its consumers
/// read the empty BSDF.
class ZeroWeightBsdfElisionRefactor final : public MaterialX::ShaderGraphRefactor {
public:
    [[nodiscard]] std::string const &getName() const override;
    std::size_t execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) override;
};

/// \brief Replaces a layer whose top is the empty BSDF by its base.
class EmptyTopLayerElisionRefactor final : public MaterialX::ShaderGraphRefactor {
public:
    [[nodiscard]] std::string const &getName() const override;
    std::size_t execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) override;
};
} // namespace flux
