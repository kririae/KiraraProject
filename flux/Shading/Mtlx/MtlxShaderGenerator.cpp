#include "flux/Shading/Mtlx/MtlxShaderGenerator.h"

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Document.h>
#include <MaterialXCore/Library.h>
#include <MaterialXCore/Material.h>
#include <MaterialXCore/Node.h>
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
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "flux/Shading/Mtlx/FluxShaderGenerator.h"
#include "flux/Shading/Mtlx/FluxShaderGraphRefactor.h"
#include "flux/Shading/Mtlx/FluxSourceCodeNode.h"
#include "flux/Shading/Mtlx/MtlxEmission.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto libraryFiles = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Shading/Mtlx/MtlxLibraries.inc"
});

/// \brief An input of a node, named by its path in the document, such as \c img/file.
///
/// \c node is null if the document has no node at the path.
struct InputPath {
    MaterialX::NodePtr node;
    std::string input;
};

[[nodiscard]] InputPath
splitInputPath(MaterialX::DocumentPtr const &document, std::string const &path) {
    auto const slash = path.rfind('/');
    if (slash == std::string::npos)
        return {};
    auto const element = document->getDescendant(path.substr(0, slash));
    return {element ? element->asA<MaterialX::Node>() : nullptr, path.substr(slash + 1)};
}

/// \brief Whether the document itself writes the value of the input \p path.
[[nodiscard]] bool writesValue(InputPath const &path) {
    return path.node && path.node->getInput(path.input);
}

[[nodiscard]] bool isImageNodeInput(InputPath const &path) {
    return path.node && path.node->getCategory() == "image";
}

/// \brief The inputs of an image node that the texture spec consumes.
///
/// The source reads none of them.
constexpr auto imageTextureInputs =
    std::to_array<std::string_view>({"uaddressmode", "vaddressmode", "filtertype", "default"});

[[nodiscard]] bool isImageTextureInput(InputPath const &path) {
    return isImageNodeInput(path) &&
           std::ranges::find(imageTextureInputs, path.input) != imageTextureInputs.end();
}

/// \brief The geometric property that \c geompropvalue reads, the texture coordinates.
constexpr auto texCoordProperty = std::string_view{"st"};

[[nodiscard]] bool isGeomPropInput(InputPath const &path) {
    return path.node && path.node->getCategory() == "geompropvalue" && path.input == "geomprop";
}

/// \brief Whether \p path is the \c default input of \c geompropvalue.
[[nodiscard]] bool isGeomPropDefaultInput(InputPath const &path) {
    return path.node && path.node->getCategory() == "geompropvalue" && path.input == "default";
}

/// \brief Checks that \p socket names the geometric property that \c geompropvalue reads.
///
/// \throw MaterialX::ExceptionShaderGenError if \p socket names another property.
void requireTexCoordProperty(MaterialX::ShaderGraphInputSocket const &socket) {
    auto const name = socket.getValue() ? socket.getValue()->getValueString() : std::string{};
    if (name != texCoordProperty)
        throw MaterialX::ExceptionShaderGenError{fmt::format(
            "'{}' names the geometric property '{}', and only '{}' is supported", socket.getPath(),
            name, texCoordProperty
        )};
}

/// \brief Checks that no \c geompropvalue node takes its \c geomprop from another node.
///
/// \throw MaterialX::ExceptionShaderGenError if one does.
void requireWrittenGeomProp(MaterialX::ShaderGraph const &graph) {
    for (auto const *node : graph.getNodes()) {
        auto const *input = node->getInput("geomprop");
        if (input && input->getConnection() && input->getConnection()->getNode() != &graph)
            throw MaterialX::ExceptionShaderGenError{fmt::format(
                "'{}/{}' is connected, and only a value that names the geometric property '{}' is "
                "supported",
                node->getName(), input->getName(), texCoordProperty
            )};
    }
}

/// \brief Whether the emission reads \p input.
[[nodiscard]] bool isRead(MaterialX::ShaderInput const &input) {
    auto const *impl =
        dynamic_cast<FluxSourceCodeNode const *>(&input.getNode()->getImplementation());
    return !impl || impl->readsInput(input.getName());
}

/// \brief Whether the emission reads an input that \p socket publishes.
[[nodiscard]] bool isRead(MaterialX::ShaderGraphInputSocket const &socket) {
    return std::ranges::any_of(socket.getConnections(), [](MaterialX::ShaderInput const *input) {
        return isRead(*input);
    });
}

/// \brief Whether \p type is the type of an EDF or a VDF.
[[nodiscard]] bool isEDFOrVDF(MaterialX::TypeDesc const &type) {
    return type == MaterialX::Type::EDF || type == MaterialX::Type::VDF;
}

/// \brief Whether \p socket publishes only inputs of nodes that output an EDF or a VDF.
[[nodiscard]] bool feedsEDFOrVDF(MaterialX::ShaderGraphInputSocket const &socket) {
    return std::ranges::all_of(socket.getConnections(), [](MaterialX::ShaderInput const *input) {
        return isEDFOrVDF(input->getNode()->getOutput()->getType());
    });
}

/// \brief Appends a diagnostic for each network of VDFs that ends in an unread input.
void reportUnreadVDF(
    MaterialX::ShaderGraph const &graph, std::string const &materialName,
    std::vector<std::string> &diagnostics
) {
    for (auto const *node : graph.getNodes()) {
        if (node->getOutput()->getType() == MaterialX::Type::VDF)
            continue;
        for (auto const *input : node->getInputs())
            if (input->getType() == MaterialX::Type::VDF && input->getConnection() &&
                !isRead(*input))
                diagnostics.push_back(
                    fmt::format(
                        "MaterialX material '{}': '{}/{}' is not read", materialName,
                        node->getName(), input->getName()
                    )
                );
    }
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

/// \brief The line that declares the variable of \p socket as the value that the words hold at
/// \p offset.
[[nodiscard]] std::string makeWordDeclaration(
    MaterialX::ShaderGenerator const &shadergen, MaterialX::ShaderGraphInputSocket const &socket,
    std::size_t count, std::size_t offset
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
    return fmt::format("{} {} = {};", slangType, socket.getVariable(), value);
}

/// \brief The line that declares the variable of \p socket as the constant that \p socket holds.
[[nodiscard]] std::string makeConstantDeclaration(
    MaterialX::ShaderGenerator const &shadergen, MaterialX::ShaderGraphInputSocket const &socket
) {
    auto const &syntax = shadergen.getSyntax();
    auto const type = socket.getType();
    auto const literal = socket.getValue() ? syntax.getValue(type, *socket.getValue())
                                           : syntax.getDefaultValue(type);
    auto line = fmt::format("{} {}", syntax.getTypeName(type), socket.getVariable());
    if (!literal.empty())
        line = "static const " + line + " = " + literal;
    return line + ";";
}

template <class T>
bool appendComponents(MaterialX::Value const &value, std::vector<float> &components) {
    if (!value.isA<T>())
        return false;
    auto const &typed = value.asA<T>();
    if constexpr (std::is_same_v<T, float>)
        components.push_back(typed);
    else
        components.insert(components.end(), typed.data(), typed.data() + T::numElements());
    return true;
}

/// \brief The components of \p value, or none if its type has no float components.
[[nodiscard]] std::vector<float> getComponents(MaterialX::Value const &value) {
    auto components = std::vector<float>{};
    appendComponents<float>(value, components) ||
        appendComponents<MaterialX::Vector2>(value, components) ||
        appendComponents<MaterialX::Vector3>(value, components) ||
        appendComponents<MaterialX::Color3>(value, components) ||
        appendComponents<MaterialX::Vector4>(value, components) ||
        appendComponents<MaterialX::Color4>(value, components);
    return components;
}

/// \brief Appends the words of \p value, or \p count zeros if the socket holds no value.
void appendWords(
    MaterialX::ValuePtr const &value, std::size_t count, std::vector<std::uint32_t> &words
) {
    if (!value) {
        words.insert(words.end(), count, 0U);
        return;
    }
    auto const components = getComponents(*value);
    if (components.empty())
        throw MaterialX::ExceptionShaderGenError{
            fmt::format("no word holds the value '{}'", value->getValueString())
        };
    std::ranges::transform(components, std::back_inserter(words), [](float component) {
        return std::bit_cast<std::uint32_t>(component);
    });
}

/// \brief The image node that reads the filename that \p socket publishes.
///
/// \throw MaterialX::ExceptionShaderGenError if \p socket feeds anything but the \c file input of
/// one image node.
[[nodiscard]] MaterialX::NodePtr getImageNode(
    MaterialX::DocumentPtr const &document, MaterialX::ShaderGraphInputSocket const &socket
) {
    auto const &connections = socket.getConnections();
    if (connections.size() == 1 && connections[0]->getName() == "file") {
        auto const element = document->getDescendant(connections[0]->getNode()->getUniqueId());
        auto node = element ? element->asA<MaterialX::Node>() : nullptr;
        if (node && node->getCategory() == "image")
            return node;
    }
    throw MaterialX::ExceptionShaderGenError{fmt::format(
        "the filename '{}' feeds more than the file of one image node", socket.getPath()
    )};
}

/// \brief The string that \p node holds for the input \p name, or an empty string if the input
/// has no value.
[[nodiscard]] std::string getInputString(MaterialX::Node const &node, std::string const &name) {
    auto const value = node.getInputValue(name);
    return value ? value->getValueString() : std::string{};
}

/// \brief The RGBA that a texture returns for the value of an image node's \c default input.
///
/// One component fills RGB, and alpha is 1 unless \p components holds it. Any other missing
/// component is 0.
[[nodiscard]] Vec4f makeFallback(std::vector<float> const &components) {
    switch (components.size()) {
    case 1: return {components[0], components[0], components[0], 1.0F};
    case 2: return {components[0], components[1], 0.0F, 1.0F};
    case 3: return {components[0], components[1], components[2], 1.0F};
    default: return {components[0], components[1], components[2], components[3]};
    }
}

/// \brief The texture that the image node \p node samples from the file \p file.
[[nodiscard]] ImageTextureSpec makeImageTextureSpec(
    MaterialX::Node const &node, std::string const &file, std::string const &materialName,
    std::vector<std::string> &diagnostics
) {
    auto const warn = [&](std::string const &message) {
        diagnostics.push_back(
            fmt::format(
                "MaterialX material '{}': '{}' {}", materialName, node.getNamePath(), message
            )
        );
    };

    auto spec = ImageTextureSpec{};
    spec.image.path = file;

    // MaterialX transforms only a color output, so the other outputs are linear.
    auto const isColor = node.getType() == "color3" || node.getType() == "color4";
    auto const fileInput = node.getInput("file");
    auto const colorSpace =
        fileInput ? fileInput->getActiveColorSpace() : node.getActiveColorSpace();
    spec.image.colorSpace = isColor ? colorSpace : std::string{linearRec709};

    auto const addressMode = [&](std::string const &input) {
        auto const mode = getInputString(node, input);
        if (mode == "clamp")
            return ImageTextureAddressMode::Clamp;
        if (mode == "mirror")
            return ImageTextureAddressMode::Mirror;
        if (mode == "constant") {
            warn(fmt::format("has {} 'constant', which becomes border", input));
            return ImageTextureAddressMode::Border;
        }
        if (mode != "periodic")
            warn(fmt::format("has {} '{}', which becomes periodic", input, mode));
        return ImageTextureAddressMode::Wrap;
    };
    spec.addressModes = {addressMode("uaddressmode"), addressMode("vaddressmode")};

    auto const filter = getInputString(node, "filtertype");
    if (filter == "closest")
        spec.filterMode = ImageTextureFilterMode::Point;
    else if (filter != "linear")
        warn(fmt::format("has filtertype '{}', which becomes linear", filter));

    // An unauthored default keeps the magenta of a native texture, not the black of the nodedef.
    if (auto const input = node.getInput("default")) {
        if (auto const fallback = input->getValue())
            spec.fallback = makeFallback(getComponents(*fallback));
        else
            warn("has a connected default, which the texture does not read");
    }

    return spec;
}

/// \brief The nodes of the document of the category \p category that \p graph generates.
[[nodiscard]] std::vector<MaterialX::NodePtr> findNodes(
    MaterialX::DocumentPtr const &document, MaterialX::ShaderGraph const &graph,
    std::string_view category
) {
    auto found = std::vector<MaterialX::NodePtr>{};
    for (auto const *shaderNode : graph.getNodes())
        for (auto const *input : shaderNode->getInputs()) {
            auto const node = splitInputPath(document, input->getPath()).node;
            if (node && node->getCategory() == category) {
                found.push_back(node);
                break;
            }
        }
    return found;
}

/// \brief Appends a diagnostic for each \c dielectric_bsdf whose retroreflection is dropped.
void reportDroppedRetroreflection(
    MaterialX::DocumentPtr const &document, MaterialX::ShaderGraph const &graph,
    std::string const &materialName, std::vector<std::string> &diagnostics
) {
    for (auto const &node : findNodes(document, graph, "dielectric_bsdf")) {
        auto const mode = getInputString(*node, "scatter_mode");
        if (getInputString(*node, "retroreflective") == "true" && mode != "R")
            diagnostics.push_back(
                fmt::format(
                    "MaterialX material '{}': '{}' has scatter_mode '{}', which drops its "
                    "retroreflective",
                    materialName, node->getNamePath(), mode
                )
            );
    }
}

/// \brief Appends a diagnostic for each \c generalized_schlick_bsdf that has no transmission.
void reportMissingTransmission(
    MaterialX::DocumentPtr const &document, MaterialX::ShaderGraph const &graph,
    std::string const &materialName, std::vector<std::string> &diagnostics
) {
    for (auto const &node : findNodes(document, graph, "generalized_schlick_bsdf")) {
        auto const mode = getInputString(*node, "scatter_mode");
        if (mode == "T" || mode == "RT")
            diagnostics.push_back(
                fmt::format(
                    "MaterialX material '{}': '{}' has scatter_mode '{}', which has no "
                    "transmission",
                    materialName, node->getNamePath(), mode
                )
            );
    }
}

/// \brief The categories of the EDFs that the generator turns into an empty EDF.
constexpr auto unsupportedEDFs = std::to_array<std::string_view>({"conical_edf", "measured_edf"});

/// \brief Appends a diagnostic for each unsupported EDF, which emits nothing.
void reportUnsupportedEDFs(
    MaterialX::DocumentPtr const &document, MaterialX::ShaderGraph const &graph,
    std::string const &materialName, std::vector<std::string> &diagnostics
) {
    for (auto const category : unsupportedEDFs)
        for (auto const &node : findNodes(document, graph, category))
            diagnostics.push_back(
                fmt::format(
                    "MaterialX material '{}': '{}' is a {}, which emits nothing", materialName,
                    node->getNamePath(), category
                )
            );
}

/// \brief The inputs of \c UsdPreviewSurface that the generated shader never reads, with the value
/// at which each asks for nothing.
constexpr auto unreadPreviewInputs =
    std::to_array<std::pair<std::string_view, float>>({{"opacity", 1.0F}, {"displacement", 0.0F}});

/// \brief Appends a diagnostic for each input of a \c UsdPreviewSurface of \p material that asks
/// for something and that the shader never reads.
void reportUnreadPreviewInputs(
    MaterialX::ElementPtr const &material, std::string const &materialName,
    std::vector<std::string> &diagnostics
) {
    auto const node = material->asA<MaterialX::Node>();
    if (!node)
        return;
    for (auto const &shader : MaterialX::getShaderNodes(node)) {
        if (shader->getCategory() != "UsdPreviewSurface")
            continue;
        for (auto const &[name, value] : unreadPreviewInputs) {
            auto const input = shader->getInput(std::string{name});
            if (!input)
                continue;
            // A connected input holds no value.
            auto const authored = input->getValue();
            if (authored && authored->isA<float>() && authored->asA<float>() == value)
                continue;
            diagnostics.push_back(
                fmt::format(
                    "MaterialX material '{}': '{}' is not read", materialName, input->getNamePath()
                )
            );
        }
    }
}

/// \brief Emits the function \p name, which declares \p declarations, evaluates the nodes of
/// \p graph, and returns the \p member of the material that the graph builds.
void emitEntryFunction(
    MaterialX::ShaderGenerator const &shadergen, MaterialX::GenContext &context,
    MaterialX::ShaderGraph const &graph, std::span<std::string const> declarations,
    std::string_view returnType, std::string_view name, std::string_view member,
    MaterialX::ShaderStage &stage
) {
    shadergen.emitLine(
        fmt::format("public {} {}(uint* words, ShadingContext ctx)", returnType, name), stage, false
    );
    stage.beginScope();
    for (auto const &line : declarations)
        shadergen.emitLine(line, stage, false);
    shadergen.emitFunctionCalls(graph, context, stage);
    shadergen.emitLine(
        fmt::format(
            "let material = {};", shadergen.getUpstreamResult(graph.getOutputSocket(), context)
        ),
        stage, false
    );
    shadergen.emitLine(fmt::format("return material.{};", member), stage, false);
    stage.endScope();
}

[[nodiscard]] MaterialX::FileSearchPath
makeSearchPath(std::span<std::filesystem::path const> folders) {
    auto searchPath = MaterialX::FileSearchPath{};
    for (auto const &folder : folders)
        searchPath.append(MaterialX::FilePath{folder.string()});
    return searchPath;
}

/// \brief The first node of \p graph that a nodegraph defines for \p target, or null.
///
/// A node that \p target implements itself is not one.
[[nodiscard]] MaterialX::NodePtr
findGraphDefinedNode(MaterialX::GraphElement const &graph, std::string const &target) {
    for (auto const &node : graph.getNodes()) {
        auto const implementation = node->getImplementation(target);
        if (implementation && implementation->isA<MaterialX::NodeGraph>())
            return node;
    }
    return nullptr;
}

/// \brief Replaces each node that a nodegraph defines by its network, in \p graph, and keeps the
/// output of each node that a subgraph output reads.
void flattenSubgraphs(MaterialX::GraphElement &graph, std::string const &target) {
    while (auto const node = findGraphDefinedNode(graph, target)) {
        auto const subgraph = node->getImplementation(target)->asA<MaterialX::NodeGraph>();
        auto const subgraphOutputs = subgraph->getOutputs();
        auto kept = std::vector<std::pair<MaterialX::PortElementPtr, std::string>>{};
        for (auto const &port : node->getDownstreamPorts()) {
            // A port with no output reads the first one.
            auto const name = port->getOutputString();
            auto const output = name.empty()
                                    ? (subgraphOutputs.empty() ? nullptr : subgraphOutputs[0])
                                    : subgraph->getOutput(name);
            if (output && output->hasOutputString())
                kept.emplace_back(port, output->getOutputString());
        }

        graph.flattenSubgraphs(target, [&](MaterialX::NodePtr const &candidate) {
            return candidate == node;
        });
        for (auto const &[port, output] : kept)
            port->setOutputString(output);
    }
}

/// \brief Replaces each node that a nodegraph defines by its network, in the document and in each
/// of its nodegraphs. A nodegraph of the data library stays.
void flattenGraphDefinedNodes(MaterialX::Document &document, std::string const &target) {
    flattenSubgraphs(document, target);
    for (auto const &child : document.getChildren())
        if (auto const graph = child->asA<MaterialX::NodeGraph>())
            flattenSubgraphs(*graph, target);
}
} // namespace

struct MtlxShaderGenerator::pImpl final {
    explicit pImpl(std::filesystem::path const &root);

    /// \brief Generates the shader of the material, and appends what it drops to \p diagnostics.
    ///
    /// \throw MaterialX::Exception If the content cannot be generated.
    [[nodiscard]] ShaderInstance makeInstance(
        MaterialX::DocumentPtr const &document, std::string const &materialName,
        MaterialX::FileSearchPath const &searchPath, std::span<std::string const> interactive,
        std::vector<std::string> &diagnostics
    ) const;

    MaterialX::FilePath mtlxRoot;
    MaterialX::DocumentPtr libraries;
    std::shared_ptr<FluxShaderGenerator> generator;
};

static_assert(
    MATERIALX_MAJOR_VERSION == 1 && MATERIALX_MINOR_VERSION == 39 && MATERIALX_BUILD_VERSION == 5,
    "The glue code of the genflux closures calls the functions of MaterialX 1.39.5's libraries"
);

MtlxShaderGenerator::pImpl::pImpl(std::filesystem::path const &root)
    : mtlxRoot{root.string()}, libraries{MaterialX::createDocument()} {
    auto const searchPath = MaterialX::FileSearchPath{mtlxRoot};
    MaterialX::StringSet loaded;
    try {
        loaded = MaterialX::loadLibraries({"libraries"}, searchPath, libraries);
    } catch (MaterialX::Exception &error) {
        throw kira::Anyhow(
            "MtlxShaderGenerator: {} holds no libraries: {}", root.string(), error.what()
        );
    }
    if (loaded.empty())
        throw kira::Anyhow("MtlxShaderGenerator: {} holds no MaterialX libraries", root.string());
    for (auto const *source : std::views::values(libraryFiles))
        MaterialX::readFromXmlString(libraries, source, searchPath);

    generator = std::make_shared<FluxShaderGenerator>(MaterialX::TypeSystem::create());
}

MtlxShaderGenerator::MtlxShaderGenerator(std::filesystem::path const &mtlxRoot)
    : pImpl_{std::make_unique<pImpl>(mtlxRoot)} {}

MtlxShaderGenerator::~MtlxShaderGenerator() = default;

MtlxShaderGenerator::GenerationResult MtlxShaderGenerator::generate(
    std::string const &document, std::string const &materialName,
    std::span<std::filesystem::path const> searchPath, std::span<std::string const> interactive
) const {
    auto result = GenerationResult{};
    try {
        auto const folders = makeSearchPath(searchPath);
        auto const parsed = MaterialX::createDocument();
        MaterialX::readFromXmlString(parsed, document, folders);
        auto const element = parsed->getChild(materialName);
        if (!element || element->getCategory() != "surfacematerial")
            return {};

        result.instance =
            pImpl_->makeInstance(parsed, materialName, folders, interactive, result.diagnostics);
    } catch (MaterialX::Exception const &error) {
        result.diagnostics.push_back(
            fmt::format("MaterialX material '{}': {}", materialName, error.what())
        );
    }
    return result;
}

ShaderInstance MtlxShaderGenerator::pImpl::makeInstance(
    MaterialX::DocumentPtr const &document, std::string const &materialName,
    MaterialX::FileSearchPath const &searchPath, std::span<std::string const> interactive,
    std::vector<std::string> &diagnostics
) const {
    MaterialX::flattenFilenames(document, searchPath);
    document->setDataLibrary(libraries);

    auto message = std::string{};
    if (!document->validate(&message))
        throw MaterialX::ExceptionShaderGenError{message};
    auto const material = document->getChild(materialName);
    reportUnreadPreviewInputs(material, materialName, diagnostics);
    flattenGraphDefinedNodes(*document, generator->getTarget());

    auto context = MaterialX::GenContext{generator};
    context.registerSourceCodeSearchPath(MaterialX::FileSearchPath{mtlxRoot});
    context.getOptions().premultipliedBsdfAdd = false;
    context.getOptions().shaderInterfaceType = MaterialX::SHADER_INTERFACE_COMPLETE;
    context.getOptions().elideConstantNodes = false;
    auto const interactiveInputs = std::make_shared<InteractiveInputs>(interactive);
    context.pushUserData(InteractiveInputs::name, interactiveInputs);

    constexpr auto sourceName = "material";
    if (material->getName() != sourceName)
        material->setName(document->createValidChildName(sourceName));
    auto const shader = generator->createShader(materialName, material, context);
    auto const &graph = shader->getGraph();
    requireWrittenGeomProp(graph);

    auto const stage = generator->makeStage();
    generator->emitLine("import flux_material;", *stage, false);
    generator->emitLine("import flux_context;", *stage, false);
    generator->emitLineBreak(*stage);
    generator->emitLibraryInclude("stdlib/genslang/lib/mx_math.slang", context, *stage);
    generator->emitLineBreak(*stage);
    generator->emitFunctionDefinitions(graph, context, *stage);

    auto declarations = std::vector<std::string>{};
    auto unmatched = std::set<std::string, std::less<>>{interactive.begin(), interactive.end()};
    auto words = std::vector<std::uint32_t>{};
    auto textures = std::vector<ImageTextureSpec>{};
    for (auto const *socket : graph.getInputSockets()) {
        auto const path = splitInputPath(document, socket->getPath());
        if (isGeomPropInput(path)) {
            requireTexCoordProperty(*socket);
            continue;
        }
        if (!isRead(*socket)) {
            if (!socket->getConnections().empty() && writesValue(path) &&
                !isImageTextureInput(path) && !isGeomPropDefaultInput(path) &&
                !feedsEDFOrVDF(*socket))
                diagnostics.push_back(
                    fmt::format(
                        "MaterialX material '{}': '{}' is not read", materialName, socket->getPath()
                    )
                );
            continue;
        }

        auto const typeDesc = socket->getType();
        auto const count = wordCount(typeDesc);
        auto const isInteractive = unmatched.erase(socket->getPath()) > 0;
        if (isInteractive && !count)
            diagnostics.push_back(
                fmt::format(
                    "MaterialX material '{}': '{}' cannot be interactive, since no word holds "
                    "the type '{}'",
                    materialName, socket->getPath(), typeDesc.getName()
                )
            );
        if (typeDesc == MaterialX::Type::FILENAME) {
            auto const image = getImageNode(document, *socket);
            declarations.push_back(
                fmt::format("uint {} = {};", socket->getVariable(), textures.size())
            );
            auto const file = socket->getValue() ? socket->getValue()->getValueString() : "";
            textures.push_back(makeImageTextureSpec(*image, file, materialName, diagnostics));
            continue;
        }
        if (!isInteractive || !count) {
            declarations.push_back(makeConstantDeclaration(*generator, *socket));
            continue;
        }

        declarations.push_back(makeWordDeclaration(*generator, *socket, *count, words.size()));
        appendWords(socket->getValue(), *count, words);
    }
    for (auto const &path : unmatched)
        diagnostics.push_back(
            fmt::format(
                "MaterialX material '{}': '{}' names no input that is read", materialName, path
            )
        );

    reportDroppedRetroreflection(document, graph, materialName, diagnostics);
    reportMissingTransmission(document, graph, materialName, diagnostics);
    reportUnsupportedEDFs(document, graph, materialName, diagnostics);
    reportUnreadVDF(graph, materialName, diagnostics);

    emitEntryFunction(
        *generator, context, graph, declarations, "IBSDF", "makeBSDF", "bsdf", *stage
    );
    emitEntryFunction(*generator, context, graph, declarations, "IEDF", "makeEDF", "edf", *stage);
    generator->substituteTokens(*stage);

    return ShaderInstance{
        .spec =
            {.source = stage->getSourceCode(), .mayEmit = flux::mayEmit(graph, *interactiveInputs)},
        .words = std::move(words),
        .textures = std::move(textures),
    };
}
} // namespace flux
