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
class FluxSourceCodeNode final : public MaterialX::SourceCodeNode {
public:
    static MaterialX::ShaderNodeImplPtr create();

    /// \throw MaterialX::ExceptionShaderGenError If the implementation names an unknown file.
    void
    initialize(MaterialX::InterfaceElement const &element, MaterialX::GenContext &context) override;

    /// \brief Whether the emission of a node of this class reads its input \p name.
    ///
    /// An inline implementation reads the inputs that its expression names. Any other
    /// implementation reads all of them.
    [[nodiscard]] bool readsInput(std::string const &name) const;

    /// \brief Emits the Slang file that the implementation names, once per stage.
    void emitFunctionDefinition(
        MaterialX::ShaderNode const &node, MaterialX::GenContext &context,
        MaterialX::ShaderStage &stage
    ) const override;

    /// \brief Copies the inline branch of MaterialX 1.39.5's \c SourceCodeNode.
    void emitFunctionCall(
        MaterialX::ShaderNode const &node, MaterialX::GenContext &context,
        MaterialX::ShaderStage &stage
    ) const override;

private:
    std::set<std::string> namedInputs_;
    std::string slangLibrary_;
};
} // namespace flux
