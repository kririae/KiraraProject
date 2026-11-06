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
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "flux/Mtlx/FluxShaderGenerator.h"
#include "flux/Mtlx/FluxSourceCodeNode.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto libraryFiles = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Mtlx/MtlxLibraries.inc"
});

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

struct WordRead {
    std::string variable;
    std::string slangType;
    std::size_t wordCount{};
    std::size_t offset{};
};

void emitWordReads(
    MaterialX::ShaderGenerator const &shadergen, std::vector<WordRead> const &reads,
    MaterialX::ShaderStage &stage
) {
    for (auto const &read : reads) {
        auto line = std::string{};
        if (read.wordCount == 1) {
            line = fmt::format(
                "{} {} = asfloat(words[{}]);", read.slangType, read.variable, read.offset
            );
        } else {
            auto const indices = std::views::iota(std::size_t{}, read.wordCount);
            auto const values = indices | std::views::transform([&](std::size_t index) {
                return fmt::format("asfloat(words[{}])", read.offset + index);
            });
            line = fmt::format(
                "{} {} = {}({});", read.slangType, read.variable, read.slangType,
                fmt::join(values, ", ")
            );
        }
        shadergen.emitLine(line, stage, false);
    }
}
} // namespace

MtlxBSDFGenerator::MtlxBSDFGenerator(
    std::filesystem::path const &mtlxRoot, MaterialX::ShaderInterfaceType interfaceType
)
    : mtlxRoot_{mtlxRoot.string()}, interfaceType_{interfaceType},
      libraries_{MaterialX::createDocument()} {
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

std::optional<ShaderAssetSpec> MtlxBSDFGenerator::generate(
    MaterialX::DocumentPtr const &document, std::string const &materialName
) const {
    auto const element = document->getChild(materialName);
    if (!element || element->getCategory() != "surfacematerial")
        return std::nullopt;

    try {
        return makeSpec(document, materialName);
    } catch (MaterialX::Exception const &) { return std::nullopt; }
}

ShaderAssetSpec MtlxBSDFGenerator::makeSpec(
    MaterialX::DocumentPtr const &document, std::string const &materialName
) const {
    auto const copy = document->copy();
    copy->setDataLibrary(libraries_);

    auto message = std::string{};
    if (!copy->validate(&message))
        throw MaterialX::ExceptionShaderGenError{message};

    auto context = MaterialX::GenContext{generator_};
    context.registerSourceCodeSearchPath(MaterialX::FileSearchPath{mtlxRoot_});
    context.getOptions().premultipliedBsdfAdd = false;
    context.getOptions().shaderInterfaceType = interfaceType_;

    auto const shader =
        generator_->createShader(materialName, copy->getChild(materialName), context);
    auto const &graph = shader->getGraph();

    auto parameters = std::vector<ShaderParameter>{};
    auto reads = std::vector<WordRead>{};
    auto offset = std::size_t{};
    for (auto const *socket : graph.getInputSockets()) {
        if (!isRead(*socket))
            continue;

        auto const typeDesc = socket->getType();
        auto const count = wordCount(typeDesc);
        if (!count)
            throw MaterialX::ExceptionShaderGenError{fmt::format(
                "'{}' is read, and no word holds the type '{}'", socket->getPath(),
                typeDesc.getName()
            )};

        parameters.push_back(
            {.path = socket->getPath(),
             .type = typeDesc.getName(),
             .wordCount = *count,
             .offset = offset}
        );
        reads.push_back(
            {.variable = socket->getVariable(),
             .slangType = generator_->getSyntax().getTypeName(typeDesc),
             .wordCount = *count,
             .offset = offset}
        );
        offset += *count;
    }

    auto const stage = generator_->makeStage();
    generator_->emitLine("import flux_bsdf;", *stage, false);
    generator_->emitLine("import flux_context;", *stage, false);
    generator_->emitLineBreak(*stage);
    generator_->emitLibraryInclude("stdlib/genslang/lib/mx_math.slang", context, *stage);
    generator_->emitLineBreak(*stage);
    generator_->emitFunctionDefinitions(graph, context, *stage);
    generator_->emitLine("public IBSDF makeBSDF(uint* words, ShadingContext ctx)", *stage, false);
    stage->beginScope();
    emitWordReads(*generator_, reads, *stage);
    generator_->emitFunctionCalls(graph, context, *stage);
    generator_->emitLine(
        fmt::format("return {};", generator_->getUpstreamResult(graph.getOutputSocket(), context)),
        *stage, false
    );
    stage->endScope();
    generator_->substituteTokens(*stage);

    return ShaderAssetSpec{.source = stage->getSourceCode(), .parameters = std::move(parameters)};
}
} // namespace flux
