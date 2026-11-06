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

/// \brief Generates the BSDF of a MaterialX material with FLux's MaterialX.
class HdFluxMaterialXGenerator final {
public:
    /// \brief A generated material: the spec, the words that its source reads, and the textures
    /// that it samples.
    struct Material {
        flux::ShaderAssetSpec spec;
        std::vector<std::uint32_t> words;
        std::vector<flux::ImageTextureSpec> textures;
    };

    /// \param mtlxRoot The folder that holds the MaterialX \c libraries folder.
    /// \throw kira::Anyhow If the libraries cannot be loaded.
    explicit HdFluxMaterialXGenerator(std::filesystem::path const &mtlxRoot);
    ~HdFluxMaterialXGenerator();

    HdFluxMaterialXGenerator(HdFluxMaterialXGenerator &&) noexcept;
    HdFluxMaterialXGenerator &operator=(HdFluxMaterialXGenerator &&) noexcept;

    /// \brief Generates the material \p surfaceMaterialName of the document \p xml.
    ///
    /// Every value of the material is a constant. Returns nothing if the material cannot be
    /// generated.
    ///
    /// \param searchPath The folders that a relative image file is found in.
    [[nodiscard]] std::optional<Material> generate(
        std::string const &xml, std::string const &surfaceMaterialName,
        std::span<std::filesystem::path const> searchPath
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
