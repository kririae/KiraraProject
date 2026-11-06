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
#include "flux/Shading/Texture.h"

namespace flux {
class FluxShaderGenerator;

/// \brief Turns the materials of MaterialX documents into the Slang of the BSDF library.
///
/// The generated source imports \c flux_bsdf and \c flux_context, which a backend provides.
class MtlxBSDFGenerator final {
public:
    struct Instance {
        ShaderAssetSpec spec;

        std::vector<std::uint32_t> words;

        /// \brief The textures that the source samples. Texture k is the one that slot k samples.
        std::vector<ImageTextureSpec> textures;
    };

    /// \brief The material, and what the generator did not honour in it.
    struct GenerationResult {
        /// \brief Nothing if the material cannot be generated.
        std::optional<Instance> instance;

        /// \brief One sentence for each thing that the generator did not honour.
        std::vector<std::string> diagnostics;
    };

    /// \brief Loads the MaterialX libraries of \p mtlxRoot, and FLux's data.
    ///
    /// \param mtlxRoot The MaterialX installation whose libraries the documents read.
    /// \throw kira::Anyhow If the libraries cannot be loaded.
    explicit MtlxBSDFGenerator(std::filesystem::path const &mtlxRoot);

    /// \brief Generates the material \p materialName of \p document.
    ///
    /// The source holds every value of the material as a constant, except the inputs in
    /// \p interactive: each of those is read from the words, which hold its current value.
    ///
    /// \param searchPath The folders that a relative image file is found in. A file that no folder
    ///        holds stays as authored.
    /// \param interactive Paths of inputs, such as \c oren/color.
    [[nodiscard]] GenerationResult generate(
        MaterialX::DocumentPtr const &document, std::string const &materialName,
        MaterialX::FileSearchPath const &searchPath, std::span<std::string const> interactive = {}
    ) const;

private:
    /// \brief Generates the material, and appends what it does not honour to \p diagnostics.
    ///
    /// \throw MaterialX::Exception If the content cannot be generated.
    [[nodiscard]] Instance makeInstance(
        MaterialX::DocumentPtr const &document, std::string const &materialName,
        MaterialX::FileSearchPath const &searchPath, std::span<std::string const> interactive,
        std::vector<std::string> &diagnostics
    ) const;

    MaterialX::FilePath mtlxRoot_;
    MaterialX::DocumentPtr libraries_;
    std::shared_ptr<FluxShaderGenerator> generator_;
};
} // namespace flux
