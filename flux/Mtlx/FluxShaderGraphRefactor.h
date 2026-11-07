#pragma once

#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/GenUserData.h>
#include <MaterialXGenShader/ShaderGraph.h>
#include <MaterialXGenShader/ShaderGraphRefactor.h>

#include <cstddef>
#include <set>
#include <span>
#include <string>

namespace flux {
/// \brief The paths of the inputs that the application changes while it renders.
///
/// The refactors of \c genflux never read the value of such an input, since it is no literal.
class InteractiveInputs final : public MaterialX::GenUserData {
public:
    /// \brief The name that a \c GenContext holds this user data under.
    static constexpr auto name = "fluxInteractiveInputs";

    explicit InteractiveInputs(std::span<std::string const> paths)
        : paths_(paths.begin(), paths.end()) {}

    [[nodiscard]] bool contains(std::string const &path) const { return paths_.contains(path); }

private:
    std::set<std::string, std::less<>> paths_;
};

/// \brief Replaces a BSDF mix whose literal mix is 0 or 1 by the BSDF that it selects.
class LiteralMixElisionRefactor final : public MaterialX::ShaderGraphRefactor {
public:
    [[nodiscard]] std::string const &getName() const override;
    std::size_t execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) override;
};

/// \brief Removes a BSDF that a literal weight below \c M_FLOAT_EPS makes empty, so its consumers
/// read the empty BSDF.
class ZeroWeightBsdfElisionRefactor final : public MaterialX::ShaderGraphRefactor {
public:
    [[nodiscard]] std::string const &getName() const override;
    std::size_t execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) override;
};

/// \brief Replaces a layer whose top is the empty BSDF by its base.
class EmptyTopLayerElisionRefactor final : public MaterialX::ShaderGraphRefactor {
public:
    [[nodiscard]] std::string const &getName() const override;
    std::size_t execute(MaterialX::ShaderGraph &graph, MaterialX::GenContext &context) override;
};
} // namespace flux
