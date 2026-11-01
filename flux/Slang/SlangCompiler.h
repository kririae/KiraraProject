#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "flux/Core/Object.h"

namespace flux {
struct SlangModule {
    /// \brief The name that an \c import names this module by.
    std::string name;

    std::string source;
};

/// \brief Compiles one Slang module, given the modules that it imports, to the code of one target.
///
/// Thread-safe. Compiles run one at a time.
class SlangCompiler final : private Noncopyable {
public:
    enum class Target : std::uint8_t {
        CudaSource,
    };

    explicit SlangCompiler(Target target);
    ~SlangCompiler();

    /// \brief Returns the target code of the module \p linkModule, compiled with \p modules.
    ///
    /// \pre Each module of \p modules comes after the modules that it imports, because a load
    /// resolves the imports that the loaded module makes.
    /// \throw std::invalid_argument If two modules of \p modules have one name, or if no module has
    /// the name \p linkModule.
    /// \throw kira::Anyhow If Slang fails, with its diagnostics and the name of the module.
    [[nodiscard]] std::string
    compile(std::span<SlangModule const> modules, std::string_view linkModule) const;

    /// \brief Returns the prelude that the code of \c compile begins with.
    ///
    /// The reference stays valid until the destruction of this compiler.
    /// \throw kira::Anyhow If Slang fails.
    [[nodiscard]] std::string const &getTargetPrelude() const;

private:
    struct pImpl;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux
