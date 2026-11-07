#pragma once

#include <string>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Slang/SlangCompiler.h"

namespace flux {
/// \brief Compiles the Slang of a BSDF into OptiX IR that defines its direct callable.
///
/// Thread-safe.
class OptixBSDFCompiler final : private Noncopyable {
public:
    /// \brief The name of the direct callable that the IR defines, of type \c OptixBSDFCallable.
    static constexpr auto callableName = "__direct_callable__bsdf";

    /// \brief Targets the virtual architecture of the current CUDA device.
    ///
    /// \throw kira::Anyhow If CUDA fails.
    OptixBSDFCompiler();

    /// \brief Returns the OptiX IR of the module \p material, which declares \c makeBSDF.
    ///
    /// The IR names its launch parameters \c optixLaunchParams.
    ///
    /// \throw kira::Anyhow If Slang or NVRTC fails, with its log.
    [[nodiscard]] std::vector<char> compile(std::string const &material) const;

private:
    std::string architectureOption_;
    SlangCompiler slangCompiler_;
};
} // namespace flux
