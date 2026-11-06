#pragma once

#include <MaterialXCore/Types.h>
#include <MaterialXGenShader/Syntax.h>
#include <MaterialXGenSlang/SlangSyntax.h>

#include <memory>

namespace flux {
/// \brief Slang's syntax, with MaterialX's closure types named as the BSDF library's interface.
class FluxSyntax final : public MaterialX::SlangSyntax {
public:
    explicit FluxSyntax(MaterialX::TypeSystemPtr typeSystem)
        : MaterialX::SlangSyntax{std::move(typeSystem)} {
        for (auto const type :
             {MaterialX::Type::BSDF, MaterialX::Type::SURFACESHADER, MaterialX::Type::MATERIAL})
            registerTypeSyntax(
                type, std::make_shared<MaterialX::AggregateTypeSyntax>(
                          this, "IBSDF", "EmptyBSDF()", MaterialX::EMPTY_STRING,
                          MaterialX::EMPTY_STRING, MaterialX::EMPTY_STRING
                      )
            );
    }
};
} // namespace flux
