#pragma once

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Types.h>
#include <MaterialXGenShader/ShaderNodeImpl.h>
#include <MaterialXGenSlang/SlangShaderGenerator.h>

#include <string>

namespace flux {
/// \brief The Slang generator of the \c genflux target.
///
/// A node that \c genflux does not implement keeps the implementation that its target inherits.
class FluxShaderGenerator final : public MaterialX::SlangShaderGenerator {
public:
    explicit FluxShaderGenerator(MaterialX::TypeSystemPtr typeSystem);

    using MaterialX::SlangShaderGenerator::createShader;

    [[nodiscard]] MaterialX::ShaderStagePtr makeStage() const;

    /// \brief Applies to \p stage the substitutions that MaterialX's Slang generator applies.
    void substituteTokens(MaterialX::ShaderStage &stage) const;

    [[nodiscard]] std::string const &getTarget() const override;

    /// \brief Returns the class that emits the implementation that \p nodeDef resolves to.
    ///
    /// \throw MaterialX::ExceptionShaderGenError If \c genflux does not implement \p nodeDef, and
    /// \p nodeDef outputs a closure or reads the state of a rasterization pipeline.
    [[nodiscard]] MaterialX::ShaderNodeImplPtr getImplementation(
        MaterialX::NodeDef const &nodeDef, MaterialX::GenContext &context
    ) const override;

    [[nodiscard]] MaterialX::ShaderNodeImplPtr createShaderNodeImplForImplementation(
        MaterialX::Implementation const &implementation
    ) const override;
};
} // namespace flux
