#include "flux/Mtlx/MtlxBSDFGenerator.h"

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Types.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXGenShader/Exception.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/GenOptions.h>
#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/ShaderGraph.h>
#include <MaterialXGenShader/ShaderStage.h>
#include <MaterialXGenShader/Syntax.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "flux/Core/Logging.h"
#include "flux/Mtlx/FluxShaderGenerator.h"
#include "flux/Mtlx/FluxSourceCodeNode.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto libraryFiles = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Mtlx/MtlxLibraries.inc"
});

/// \brief Whether \p document itself writes the value of the published input \p path.
[[nodiscard]] bool writesValue(MaterialX::DocumentPtr const &document, std::string const &path) {
    auto const slash = path.rfind('/');
    if (slash == std::string::npos)
        return false;
    auto const node = document->getDescendant(path.substr(0, slash));
    return node && node->asA<MaterialX::Node>() &&
           node->asA<MaterialX::Node>()->getInput(path.substr(slash + 1));
}

/// \brief Whether the emission reads an input that \p socket publishes.
[[nodiscard]] bool isRead(MaterialX::ShaderGraphInputSocket const &socket) {
    return std::ranges::any_of(socket.getConnections(), [](MaterialX::ShaderInput const *input) {
        auto const *impl =
            dynamic_cast<FluxSourceCodeNode const *>(&input->getNode()->getImplementation());
        return !impl || impl->readsInput(input->getName());
    });
}

[[nodiscard]] std::optional<std::size_t> wordCount(MaterialX::TypeDesc const &type) {
    if (type.getBaseType() != MaterialX::TypeDesc::BASETYPE_FLOAT)
        return std::nullopt;
    switch (type.getSize()) {
    case 1:
    case 2:
    case 3:
    case 4: return type.getSize();
    default: return std::nullopt;
    }
}

/// \brief Declares the variable of \p socket as the parameter at word \p offset.
void emitWordRead(
    MaterialX::ShaderGenerator const &shadergen, MaterialX::ShaderGraphInputSocket const &socket,
    std::size_t count, std::size_t offset, MaterialX::ShaderStage &stage
) {
    auto const slangType = shadergen.getSyntax().getTypeName(socket.getType());
    auto value = fmt::format("asfloat(words[{}])", offset);
    if (count > 1) {
        auto const indices = std::views::iota(offset, offset + count);
        auto const components = indices | std::views::transform([](std::size_t index) {
            return fmt::format("asfloat(words[{}])", index);
        });
        value = fmt::format("{}({})", slangType, fmt::join(components, ", "));
    }
    shadergen.emitLine(
        fmt::format("{} {} = {};", slangType, socket.getVariable(), value), stage, false
    );
}

/// \brief Declares the variable of \p socket as the constant that \p socket holds.
void emitConstant(
    MaterialX::ShaderGenerator const &shadergen, MaterialX::ShaderGraphInputSocket const &socket,
    MaterialX::ShaderStage &stage
) {
    auto const &syntax = shadergen.getSyntax();
    auto const type = socket.getType();
    auto const literal = socket.getValue() ? syntax.getValue(type, *socket.getValue())
                                           : syntax.getDefaultValue(type);
    auto line = fmt::format("{} {}", syntax.getTypeName(type), socket.getVariable());
    if (!literal.empty())
        line += " = " + literal;
    shadergen.emitLine(line + ";", stage, false);
}

template <class T>
bool appendComponents(MaterialX::Value const &value, std::vector<std::uint32_t> &words) {
    if (!value.isA<T>())
        return false;
    auto const &typed = value.asA<T>();
    if constexpr (std::is_same_v<T, float>)
        words.push_back(std::bit_cast<std::uint32_t>(typed));
    else
        for (auto const component : std::span{typed.data(), T::numElements()})
            words.push_back(std::bit_cast<std::uint32_t>(component));
    return true;
}

/// \brief Appends the words of \p value, or \p count zeros if the socket holds no value.
void appendWords(
    MaterialX::ValuePtr const &value, std::size_t count, std::vector<std::uint32_t> &words
) {
    if (!value) {
        words.insert(words.end(), count, 0U);
        return;
    }
    auto const appended = appendComponents<float>(*value, words) ||
                          appendComponents<MaterialX::Vector2>(*value, words) ||
                          appendComponents<MaterialX::Vector3>(*value, words) ||
                          appendComponents<MaterialX::Color3>(*value, words) ||
                          appendComponents<MaterialX::Vector4>(*value, words) ||
                          appendComponents<MaterialX::Color4>(*value, words);
    if (!appended)
        throw MaterialX::ExceptionShaderGenError{
            fmt::format("no word holds the value '{}'", value->getValueString())
        };
}
} // namespace

MtlxBSDFGenerator::MtlxBSDFGenerator(std::filesystem::path const &mtlxRoot)
    : mtlxRoot_{mtlxRoot.string()}, libraries_{MaterialX::createDocument()} {
    auto const searchPath = MaterialX::FileSearchPath{mtlxRoot_};
    MaterialX::StringSet loaded;
    try {
        loaded = MaterialX::loadLibraries({"libraries"}, searchPath, libraries_);
    } catch (MaterialX::Exception &error) {
        throw kira::Anyhow(
            "MtlxBSDFGenerator: {} holds no libraries: {}", mtlxRoot.string(), error.what()
        );
    }
    if (loaded.empty())
        throw kira::Anyhow("MtlxBSDFGenerator: {} holds no MaterialX libraries", mtlxRoot.string());
    for (auto const *source : std::views::values(libraryFiles))
        MaterialX::readFromXmlString(libraries_, source, searchPath);

    generator_ = std::make_shared<FluxShaderGenerator>(MaterialX::TypeSystem::create());
}

std::optional<MtlxBSDFGenerator::Instance> MtlxBSDFGenerator::generate(
    MaterialX::DocumentPtr const &document, std::string const &materialName,
    std::span<std::string const> interactive
) const {
    auto const element = document->getChild(materialName);
    if (!element || element->getCategory() != "surfacematerial")
        return std::nullopt;

    try {
        return makeInstance(document, materialName, interactive);
    } catch (MaterialX::Exception const &error) {
        LogWarn("MaterialX material '{}': {}", materialName, error.what());
        return std::nullopt;
    }
}

MtlxBSDFGenerator::Instance MtlxBSDFGenerator::makeInstance(
    MaterialX::DocumentPtr const &document, std::string const &materialName,
    std::span<std::string const> interactive
) const {
    auto const copy = document->copy();
    copy->setDataLibrary(libraries_);

    auto message = std::string{};
    if (!copy->validate(&message))
        throw MaterialX::ExceptionShaderGenError{message};

    auto context = MaterialX::GenContext{generator_};
    context.registerSourceCodeSearchPath(MaterialX::FileSearchPath{mtlxRoot_});
    context.getOptions().premultipliedBsdfAdd = false;
    context.getOptions().shaderInterfaceType = MaterialX::SHADER_INTERFACE_COMPLETE;
    context.getOptions().elideConstantNodes = false;

    auto const shader =
        generator_->createShader(materialName, copy->getChild(materialName), context);
    auto const &graph = shader->getGraph();

    auto const stage = generator_->makeStage();
    generator_->emitLine("import flux_bsdf;", *stage, false);
    generator_->emitLine("import flux_context;", *stage, false);
    generator_->emitLineBreak(*stage);
    generator_->emitLibraryInclude("stdlib/genslang/lib/mx_math.slang", context, *stage);
    generator_->emitLineBreak(*stage);
    generator_->emitFunctionDefinitions(graph, context, *stage);
    generator_->emitLine("public IBSDF makeBSDF(uint* words, ShadingContext ctx)", *stage, false);
    stage->beginScope();

    auto unmatched = std::set<std::string, std::less<>>{interactive.begin(), interactive.end()};
    auto parameters = std::vector<ShaderParameter>{};
    auto words = std::vector<std::uint32_t>{};
    for (auto const *socket : graph.getInputSockets()) {
        if (!isRead(*socket)) {
            if (!socket->getConnections().empty() && writesValue(copy, socket->getPath()))
                LogWarn(
                    "MaterialX material '{}': '{}' is not read", materialName, socket->getPath()
                );
            continue;
        }

        auto const typeDesc = socket->getType();
        auto const count = wordCount(typeDesc);
        auto const isInteractive = unmatched.erase(socket->getPath()) > 0;
        if (isInteractive && !count)
            LogWarn(
                "MaterialX material '{}': '{}' cannot be interactive, since no word holds the type "
                "'{}'",
                materialName, socket->getPath(), typeDesc.getName()
            );
        if (!isInteractive || !count) {
            emitConstant(*generator_, *socket, *stage);
            continue;
        }

        parameters.push_back(
            {.path = socket->getPath(),
             .type = typeDesc.getName(),
             .wordCount = *count,
             .offset = words.size()}
        );
        emitWordRead(*generator_, *socket, *count, words.size(), *stage);
        appendWords(socket->getValue(), *count, words);
    }
    for (auto const &path : unmatched)
        LogWarn("MaterialX material '{}': '{}' names no input that is read", materialName, path);

    generator_->emitFunctionCalls(graph, context, *stage);
    generator_->emitLine(
        fmt::format("return {};", generator_->getUpstreamResult(graph.getOutputSocket(), context)),
        *stage, false
    );
    stage->endScope();
    generator_->substituteTokens(*stage);

    return Instance{
        .spec = {.source = stage->getSourceCode(), .parameters = std::move(parameters)},
        .words = std::move(words)
    };
}
} // namespace flux
