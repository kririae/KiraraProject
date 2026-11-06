#pragma once

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
/// \brief Turns the materials of MaterialX documents into the Slang of the BSDF library.
///
/// The generated source imports \c flux_bsdf and \c flux_context, which a backend provides.
///
/// Calls to \c generate must not overlap.
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

    ~MtlxBSDFGenerator();

    /// \brief Generates the material \p materialName of the MaterialX document \p document, in XML.
    [[nodiscard]] GenerationResult generate(
        std::string const &document, std::string const &materialName,
        std::span<std::filesystem::path const> searchPath,
        std::span<std::string const> interactive = {}
    ) const;

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
