#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "flux/Scene/ShaderAsset.h"

namespace flux {
/// \brief Turns a MaterialX material into its shader.
///
/// The generated source defines \c makeBSDF and \c makeEDF. It imports \c flux_material and
/// \c flux_context, which a backend provides.
///
/// Calls to \c generate must not overlap.
class MtlxShaderGenerator final {
public:
    /// \brief The shader of the material, and what the generator did not honour in it.
    struct GenerationResult {
        /// \brief Nothing if the shader cannot be generated.
        std::optional<ShaderInstance> instance;

        /// \brief One sentence for each thing that the generator did not honour.
        std::vector<std::string> diagnostics;
    };

    /// \brief Loads the MaterialX libraries of \p mtlxRoot, and FLux's data.
    ///
    /// \param mtlxRoot The MaterialX installation whose libraries the documents read.
    /// \throw kira::Anyhow If the libraries cannot be loaded.
    explicit MtlxShaderGenerator(std::filesystem::path const &mtlxRoot);

    ~MtlxShaderGenerator();

    /// \brief Generates the shader of the material \p materialName of the MaterialX document
    /// \p document, in XML.
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
