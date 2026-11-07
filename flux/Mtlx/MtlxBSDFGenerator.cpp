#include "flux/Mtlx/MtlxBSDFGenerator.h"

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Document.h>
#include <MaterialXCore/Library.h>
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

#include "flux/Mtlx/FluxShaderGenerator.h"
#include "flux/Mtlx/FluxSourceCodeNode.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
constexpr auto libraryFiles = std::to_array<std::pair<char const *, char const *>>({
#include "flux/Mtlx/MtlxLibraries.inc"
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

/// \brief Appends a diagnostic for each network of EDFs or VDFs that ends in an unread input.
void reportUnreadEDFAndVDF(
    MaterialX::ShaderGraph const &graph, std::string const &materialName,
    std::vector<std::string> &diagnostics
) {
    for (auto const *node : graph.getNodes()) {
        if (isEDFOrVDF(node->getOutput()->getType()))
            continue;
        for (auto const *input : node->getInputs())
            if (isEDFOrVDF(input->getType()) && input->getConnection() && !isRead(*input))
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

/// \brief Declares the variable of \p socket as the value that the words hold at \p offset.
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
    if (isColor && colorSpace == "srgb_texture")
        spec.image.colorSpace = ImageColorSpace::SRGB;
    else if (isColor && !colorSpace.empty() && colorSpace != "lin_rec709")
        warn(fmt::format("has the color space '{}', which becomes linear", colorSpace));

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

    auto const fallback = node.getInputValue("default");
    if (fallback)
        spec.fallback = makeFallback(getComponents(*fallback));
    else
        warn("has a connected default, which the texture does not read");

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

[[nodiscard]] MaterialX::FileSearchPath
makeSearchPath(std::span<std::filesystem::path const> folders) {
    auto searchPath = MaterialX::FileSearchPath{};
    for (auto const &folder : folders)
        searchPath.append(MaterialX::FilePath{folder.string()});
    return searchPath;
}

/// \brief Replaces each node that a nodegraph defines by its network, in the document and in each
/// of its nodegraphs.
///
/// A node that \p target implements itself stays. A nodegraph of the data library stays too.
void flattenGraphDefinedNodes(MaterialX::Document &document, std::string const &target) {
    document.flattenSubgraphs(target);
    for (auto const &child : document.getChildren())
        if (auto const graph = child->asA<MaterialX::NodeGraph>())
            graph->flattenSubgraphs(target);
}
} // namespace

struct MtlxBSDFGenerator::pImpl final {
    explicit pImpl(std::filesystem::path const &root);

    /// \brief Generates the material, and appends what it does not honour to \p diagnostics.
    ///
    /// \throw MaterialX::Exception If the content cannot be generated.
    [[nodiscard]] Instance makeInstance(
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

MtlxBSDFGenerator::pImpl::pImpl(std::filesystem::path const &root)
    : mtlxRoot{root.string()}, libraries{MaterialX::createDocument()} {
    auto const searchPath = MaterialX::FileSearchPath{mtlxRoot};
    MaterialX::StringSet loaded;
    try {
        loaded = MaterialX::loadLibraries({"libraries"}, searchPath, libraries);
    } catch (MaterialX::Exception &error) {
        throw kira::Anyhow(
            "MtlxBSDFGenerator: {} holds no libraries: {}", root.string(), error.what()
        );
    }
    if (loaded.empty())
        throw kira::Anyhow("MtlxBSDFGenerator: {} holds no MaterialX libraries", root.string());
    for (auto const *source : std::views::values(libraryFiles))
        MaterialX::readFromXmlString(libraries, source, searchPath);

    generator = std::make_shared<FluxShaderGenerator>(MaterialX::TypeSystem::create());
}

MtlxBSDFGenerator::MtlxBSDFGenerator(std::filesystem::path const &mtlxRoot)
    : pImpl_{std::make_unique<pImpl>(mtlxRoot)} {}

MtlxBSDFGenerator::~MtlxBSDFGenerator() = default;

MtlxBSDFGenerator::GenerationResult MtlxBSDFGenerator::generate(
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

MtlxBSDFGenerator::Instance MtlxBSDFGenerator::pImpl::makeInstance(
    MaterialX::DocumentPtr const &document, std::string const &materialName,
    MaterialX::FileSearchPath const &searchPath, std::span<std::string const> interactive,
    std::vector<std::string> &diagnostics
) const {
    MaterialX::flattenFilenames(document, searchPath);
    document->setDataLibrary(libraries);

    auto message = std::string{};
    if (!document->validate(&message))
        throw MaterialX::ExceptionShaderGenError{message};
    flattenGraphDefinedNodes(*document, generator->getTarget());

    auto context = MaterialX::GenContext{generator};
    context.registerSourceCodeSearchPath(MaterialX::FileSearchPath{mtlxRoot});
    context.getOptions().premultipliedBsdfAdd = false;
    context.getOptions().shaderInterfaceType = MaterialX::SHADER_INTERFACE_COMPLETE;
    context.getOptions().elideConstantNodes = false;

    auto const shader =
        generator->createShader(materialName, document->getChild(materialName), context);
    auto const &graph = shader->getGraph();
    requireWrittenGeomProp(graph);

    auto const stage = generator->makeStage();
    generator->emitLine("import flux_bsdf;", *stage, false);
    generator->emitLine("import flux_context;", *stage, false);
    generator->emitLineBreak(*stage);
    generator->emitLibraryInclude("stdlib/genslang/lib/mx_math.slang", context, *stage);
    generator->emitLineBreak(*stage);
    generator->emitFunctionDefinitions(graph, context, *stage);
    generator->emitLine("public IBSDF makeBSDF(uint* words, ShadingContext ctx)", *stage, false);
    stage->beginScope();

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
                !isImageTextureInput(path) && !feedsEDFOrVDF(*socket))
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
            generator->emitLine(
                fmt::format("uint {} = {};", socket->getVariable(), textures.size()), *stage, false
            );
            auto const file = socket->getValue() ? socket->getValue()->getValueString() : "";
            textures.push_back(makeImageTextureSpec(*image, file, materialName, diagnostics));
            continue;
        }
        if (!isInteractive || !count) {
            emitConstant(*generator, *socket, *stage);
            continue;
        }

        emitWordRead(*generator, *socket, *count, words.size(), *stage);
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
    reportUnreadEDFAndVDF(graph, materialName, diagnostics);

    generator->emitFunctionCalls(graph, context, *stage);
    generator->emitLine(
        fmt::format("return {};", generator->getUpstreamResult(graph.getOutputSocket(), context)),
        *stage, false
    );
    stage->endScope();
    generator->substituteTokens(*stage);

    return Instance{
        .spec = {.source = stage->getSourceCode()},
        .words = std::move(words),
        .textures = std::move(textures)
    };
}
} // namespace flux
