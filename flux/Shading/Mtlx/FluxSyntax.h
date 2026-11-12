#pragma once

#include <MaterialXCore/Types.h>
#include <MaterialXGenShader/Syntax.h>
#include <MaterialXGenSlang/SlangSyntax.h>

#include <memory>
#include <string>

namespace flux {
/// \brief Slang's syntax, with MaterialX's closure types named as the material library's interface.
class FluxSyntax final : public MaterialX::SlangSyntax {
public:
    explicit FluxSyntax(MaterialX::TypeSystemPtr typeSystem)
        : MaterialX::SlangSyntax{std::move(typeSystem)} {
        registerClosureType(MaterialX::Type::BSDF, "IBSDF", "EmptyBSDF()");
        registerClosureType(MaterialX::Type::EDF, "IEDF", "EmptyEDF()");
        for (auto const type : {MaterialX::Type::SURFACESHADER, MaterialX::Type::MATERIAL})
            registerClosureType(
                type, "Material<EmptyBSDF, EmptyEDF>", "makeMaterial(EmptyBSDF(), EmptyEDF())"
            );
    }

private:
    void registerClosureType(
        MaterialX::TypeDesc const &type, std::string const &name, std::string const &empty
    ) {
        registerTypeSyntax(
            type, std::make_shared<MaterialX::AggregateTypeSyntax>(
                      this, name, empty, MaterialX::EMPTY_STRING, MaterialX::EMPTY_STRING,
                      MaterialX::EMPTY_STRING
                  )
        );
    }
};
} // namespace flux
