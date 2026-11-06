#pragma once

#include <MaterialXCore/Interface.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/Nodes/SourceCodeNode.h>
#include <MaterialXGenShader/ShaderNode.h>
#include <MaterialXGenShader/ShaderStage.h>

#include <set>
#include <string>

namespace flux {
/// \brief A \c SourceCodeNode whose inline implementations declare their values with \c let.
///
/// \c emitFunctionCall copies the inline branch of MaterialX 1.39.5's \c SourceCodeNode.
class FluxSourceCodeNode final : public MaterialX::SourceCodeNode {
public:
    static MaterialX::ShaderNodeImplPtr create();

    void
    initialize(MaterialX::InterfaceElement const &element, MaterialX::GenContext &context) override;

    /// \brief Whether the emission of a node of this class reads its input \p name.
    ///
    /// An inline implementation reads the inputs that its expression names. Any other
    /// implementation reads all of them.
    [[nodiscard]] bool readsInput(std::string const &name) const;

    void emitFunctionCall(
        MaterialX::ShaderNode const &node, MaterialX::GenContext &context,
        MaterialX::ShaderStage &stage
    ) const override;

private:
    std::set<std::string> namedInputs_;
};
} // namespace flux
