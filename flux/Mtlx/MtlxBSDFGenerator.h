#pragma once

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/File.h>
#include <MaterialXGenShader/GenOptions.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "flux/Scene/ShaderAsset.h"

namespace flux {
class FluxShaderGenerator;

/// \brief Turns the materials of MaterialX documents into the Slang of the BSDF library.
///
/// The generated source imports \c flux_bsdf and \c flux_context, which a backend provides.
class MtlxBSDFGenerator final {
public:
    /// \brief Loads the MaterialX libraries of \p mtlxRoot, and FLux's data.
    ///
    /// \param mtlxRoot The MaterialX installation whose libraries the documents read.
    /// \param interfaceType The inputs that MaterialX publishes as the shader's interface.
    /// \throw kira::Anyhow If the libraries cannot be loaded.
    MtlxBSDFGenerator(
        std::filesystem::path const &mtlxRoot, MaterialX::ShaderInterfaceType interfaceType
    );

    /// \brief Returns the spec of the material \p materialName of \p document.
    ///
    /// \return Nothing if the material is absent or unsupported.
    [[nodiscard]] std::optional<ShaderAssetSpec>
    generate(MaterialX::DocumentPtr const &document, std::string const &materialName) const;

private:
    /// \throw MaterialX::Exception If the content cannot be generated.
    [[nodiscard]] ShaderAssetSpec
    makeSpec(MaterialX::DocumentPtr const &document, std::string const &materialName) const;

    MaterialX::FilePath mtlxRoot_;
    MaterialX::ShaderInterfaceType interfaceType_;
    MaterialX::DocumentPtr libraries_;
    std::shared_ptr<FluxShaderGenerator> generator_;
};
} // namespace flux
