#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flux/Scene/ShaderAsset.h"
#include "flux/Shading/Texture.h"

/// \brief Generates the BSDF of a MaterialX material with FLux's MaterialX.
class HdFluxMaterialXGenerator final {
public:
    struct Material {
        flux::ShaderAssetSpec spec;
        std::vector<std::uint32_t> words;
        std::vector<flux::ImageTextureSpec> textures;
    };

    /// \param mtlxRoot The folder that holds the MaterialX \c libraries folder.
    /// \throw kira::Anyhow If the libraries cannot be loaded.
    explicit HdFluxMaterialXGenerator(std::filesystem::path const &mtlxRoot);
    ~HdFluxMaterialXGenerator();

    /// \brief Generates the material \p surfaceMaterialName of the document \p xml.
    ///
    /// Every value of the material is a constant. Returns nothing if the material cannot be
    /// generated.
    [[nodiscard]] std::optional<Material>
    generate(std::string const &xml, std::string const &surfaceMaterialName) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
