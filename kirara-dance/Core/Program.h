#pragma once

#include <slang-gfx.h>

#include "Core/Object.h"

using Slang::ComPtr;

namespace krd {
class ProgramBuilder;

/// The immutable shader program created by \c ProgramBuilder.
///
/// \remark The shader program itself is immutable, and shouldn't be changed. New \c Program can
/// only be created by \c ProgramBuilder.
class Program final : public Object {
private:
    Program() = default;

public:
    friend class ProgramBuilder;
    ~Program() override = default;

public:
    ///
    [[nodiscard]] slang::ShaderReflection *getReflection() const {
        return linkedProgram->getLayout();
    }

    ///
    [[nodiscard]] slang::TypeReflection *getTypeReflection(std::string_view name) const {
        return getReflection()->findTypeByName(name.data());
    }

    ///
    [[nodiscard]] ComPtr<gfx::IShaderProgram> getShaderProgram() const { return shaderProgram; }

private:
    ///
    ComPtr<slang::IComponentType> linkedProgram;

    ///
    ComPtr<gfx::IShaderProgram> shaderProgram;
};
} // namespace krd
