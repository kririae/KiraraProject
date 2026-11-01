#pragma once

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/File.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "flux/Scene/ShaderAsset.h"

namespace flux {
class FluxShaderGenerator;

/// \brief Turns the materials of MaterialX documents into the Slang of the BSDF library.
///
/// The generated source imports \c flux_bsdf and \c flux_context, which a backend provides.
class MtlxBSDFGenerator final {
public:
    /// \brief One input of a material, and where its value lives in the words.
    struct Parameter {
        /// \brief The path of the input inside the material, such as \c oren/color.
        std::string path;

        /// \brief The type of the value, such as \c color3.
        std::string type;

        std::size_t wordCount{};

        /// \brief The index of its first word.
        std::size_t offset{};
    };

    struct Instance {
        ShaderAssetSpec spec;

        std::vector<Parameter> parameters;

        std::vector<std::uint32_t> words;
    };

    /// \brief Loads the MaterialX libraries of \p mtlxRoot, and FLux's data.
    ///
    /// \param mtlxRoot The MaterialX installation whose libraries the documents read.
    /// \throw kira::Anyhow If the libraries cannot be loaded.
    explicit MtlxBSDFGenerator(std::filesystem::path const &mtlxRoot);

    /// \brief Generates the material \p materialName of \p document.
    ///
    /// The source holds every value of the material as a constant, except the inputs in
    /// \p interactive: each of those becomes a parameter, and its words hold its current value.
    /// An authored value that the material never reads warns.
    ///
    /// \param interactive Paths of inputs, such as \c oren/color. A path that names no input the
    ///        material reads, or an input that words cannot hold, warns and is ignored.
    /// \return Nothing if the material is absent. Nothing, with a warning, if MaterialX cannot
    ///         build it or \c genflux cannot run one of its nodes.
    [[nodiscard]] std::optional<Instance> generate(
        MaterialX::DocumentPtr const &document, std::string const &materialName,
        std::span<std::string const> interactive = {}
    ) const;

private:
    /// \throw MaterialX::Exception If the content cannot be generated.
    [[nodiscard]] Instance makeInstance(
        MaterialX::DocumentPtr const &document, std::string const &materialName,
        std::span<std::string const> interactive
    ) const;

    MaterialX::FilePath mtlxRoot_;
    MaterialX::DocumentPtr libraries_;
    std::shared_ptr<FluxShaderGenerator> generator_;
};
} // namespace flux
