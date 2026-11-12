#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "flux/Core/Object.h"

namespace flux {
/// \brief Compiles the Slang of a shader into OptiX IR that defines the direct callables of its
/// BSDF and its EDF.
///
/// Thread-safe. Each thread compiles with its own Slang global session, which it creates on its
/// first compile, so compiles on different threads run concurrently.
class OptixShaderCompiler final : private Noncopyable {
public:
    /// \brief The name of the direct callable that the IR defines, of type \c OptixBSDFCallable.
    static constexpr auto bsdfCallableName = "__direct_callable__bsdf";

    /// \brief The name of the direct callable of the EDF, of type \c OptixEDFCallable.
    static constexpr auto edfCallableName = "__direct_callable__edf";

    /// \brief The OptiX IR of a shader, and what it took to make it.
    struct CompilationResult {
        std::vector<char> ir;

        /// \brief The name of the NVRTC program, which the errors of NVRTC cite.
        std::string programName;

        std::chrono::nanoseconds slangTime{};
        std::chrono::nanoseconds nvrtcTime{};
    };

    /// \brief Targets the virtual architecture of the current CUDA device.
    ///
    /// \throw kira::Anyhow If CUDA fails.
    OptixShaderCompiler();
    ~OptixShaderCompiler();

    /// \brief Compiles the shader \p source, which declares \c makeBSDF and \c makeEDF.
    ///
    /// The IR names its launch parameters \c optixLaunchParams.
    ///
    /// \throw kira::Anyhow If Slang or NVRTC fails, with its log.
    [[nodiscard]] CompilationResult compile(std::string const &source) const;

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
