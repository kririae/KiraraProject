#include "flux/Mtlx/FluxSourceCodeNode.h"

#include <MaterialXGenShader/Exception.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/ShaderGenerator.h>
#include <MaterialXGenShader/ShaderNode.h>
#include <MaterialXGenShader/ShaderStage.h>
#include <MaterialXGenShader/Syntax.h>
#include <MaterialXGenShader/Util.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace flux {
namespace {
constexpr auto variablePrefix = "{{";
constexpr auto variableSuffix = "}}";

constexpr auto slangLibraryFiles = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Mtlx/MtlxSlangLibraries.inc"
});

/// \brief The source of the Slang file \p name of \c Mtlx/libraries, or null if there is none.
[[nodiscard]] char const *findSlangLibrary(std::string_view name) {
    auto const found = std::ranges::find_if(slangLibraryFiles, [&](auto const &entry) {
        return std::filesystem::path{entry.first}.filename() == name;
    });
    return found == slangLibraryFiles.end() ? nullptr : found->second;
}

/// \brief The file that \p line includes, or an empty name if \p line includes none.
[[nodiscard]] std::string_view
getIncludedFile(std::string_view line, MaterialX::Syntax const &syntax) {
    if (line.find(syntax.getIncludeStatement()) == std::string_view::npos)
        return {};
    auto const quote = std::string_view{syntax.getStringQuote()};
    auto const begin = line.find_first_of(quote);
    auto const end = line.find_last_of(quote);
    if (begin == std::string_view::npos || end <= begin)
        return {};
    return line.substr(begin + 1, end - begin - 1);
}

/// \brief Emits the Slang file \p file and the ones that it includes, unless \p stage holds them.
void emitSlangLibrary(
    std::string const &file, MaterialX::GenContext &context, MaterialX::ShaderStage &stage
) {
    if (stage.hasSourceDependency(file))
        return;
    stage.addSourceDependency(file);

    auto const &shadergen = context.getShaderGenerator();
    auto lines = std::istringstream{findSlangLibrary(file)};
    for (std::string line; std::getline(lines, line);) {
        auto const included = getIncludedFile(line, shadergen.getSyntax());
        if (findSlangLibrary(included))
            emitSlangLibrary(std::string{included}, context, stage);
        else
            shadergen.emitBlock(line, file, context, stage);
    }
}
} // namespace

MaterialX::ShaderNodeImplPtr FluxSourceCodeNode::create() {
    return std::make_shared<FluxSourceCodeNode>();
}

void FluxSourceCodeNode::initialize(
    MaterialX::InterfaceElement const &element, MaterialX::GenContext &context
) {
    SourceCodeNode::initialize(element, context);

    if (!_inlined)
        return;

    auto const file = element.getAttribute("file");
    if (!file.empty() && !findSlangLibrary(file))
        throw MaterialX::ExceptionShaderGenError{
            "Implementation '" + element.getName() + "' names the unknown file '" + file + "'"
        };
    slangLibrary_ = file;
    if (!file.empty())
        _hash = std::hash<std::string>{}(file);

    auto begin = _functionSource.find(variablePrefix);
    while (begin != std::string::npos) {
        auto const end = _functionSource.find(variableSuffix, begin + 2);
        if (end == std::string::npos)
            return;
        namedInputs_.insert(_functionSource.substr(begin + 2, end - begin - 2));
        begin = _functionSource.find(variablePrefix, end + 2);
    }
}

void FluxSourceCodeNode::emitFunctionDefinition(
    MaterialX::ShaderNode const &node, MaterialX::GenContext &context, MaterialX::ShaderStage &stage
) const {
    if (slangLibrary_.empty()) {
        SourceCodeNode::emitFunctionDefinition(node, context, stage);
        return;
    }

    DEFINE_SHADER_STAGE(stage, MaterialX::Stage::PIXEL) {
        emitSlangLibrary(slangLibrary_, context, stage);
    }
}

bool FluxSourceCodeNode::readsInput(std::string const &name) const {
    return !_inlined || namedInputs_.contains(name);
}

void FluxSourceCodeNode::emitFunctionCall(
    MaterialX::ShaderNode const &node, MaterialX::GenContext &context, MaterialX::ShaderStage &stage
) const {
    using namespace MaterialX;

    if (!_inlined) {
        SourceCodeNode::emitFunctionCall(node, context, stage);
        return;
    }

    DEFINE_SHADER_STAGE(stage, Stage::PIXEL) {
        ShaderGenerator const &shadergen = context.getShaderGenerator();

        if (nodeOutputIsClosure(node)) {
            shadergen.emitDependentFunctionCalls(
                node, context, stage, ShaderNode::Classification::CLOSURE
            );
        }

        size_t pos = 0;
        size_t i = _functionSource.find(variablePrefix);
        StringSet variableNames;
        StringVec code;
        while (i != string::npos) {
            code.push_back(_functionSource.substr(pos, i - pos));
            size_t j = _functionSource.find(variableSuffix, i + 2);
            if (j == string::npos) {
                throw ExceptionShaderGenError(
                    "Malformed inline expression in implementation for node " + node.getName()
                );
            }

            string const variable = _functionSource.substr(i + 2, j - i - 2);
            ShaderInput const *input = node.getInput(variable);
            if (!input) {
                throw ExceptionShaderGenError(
                    "Could not find an input named '" + variable + "' on node '" + node.getName() +
                    "'"
                );
            }

            if (input->getConnection()) {
                code.push_back(shadergen.getUpstreamResult(input, context));
            } else {
                string variableName = node.getName() + "_" + input->getName() + "_tmp";
                if (!variableNames.count(variableName)) {
                    ShaderPort v(nullptr, input->getType(), variableName, input->getValue());
                    shadergen.emitLineBegin(stage);
                    Syntax const &syntax = shadergen.getSyntax();
                    string const valueStr =
                        (v.getValue() ? syntax.getValue(v.getType(), *v.getValue())
                                      : syntax.getDefaultValue(v.getType()));
                    string str = "let " + v.getVariable();
                    str += valueStr.empty() ? EMPTY_STRING : " = " + valueStr;
                    shadergen.emitString(str, stage);
                    shadergen.emitLineEnd(stage);
                    variableNames.insert(variableName);
                }
                code.push_back(variableName);
            }

            pos = j + 2;
            i = _functionSource.find(variablePrefix, pos);
        }
        code.push_back(_functionSource.substr(pos));

        shadergen.emitLineBegin(stage);
        shadergen.emitString("let " + node.getOutput()->getVariable() + " = ", stage);
        for (string const &c : code)
            shadergen.emitString(c, stage);
        shadergen.emitLineEnd(stage);
    }
}
} // namespace flux
