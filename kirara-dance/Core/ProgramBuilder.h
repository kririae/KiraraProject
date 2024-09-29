#pragma once

#include <kira/SmallVector.h>

#include <slang-gfx.h>
#include <slang.h>

#include <filesystem>

#include "Core/Program.h"

using Slang::ComPtr;

namespace krd {
/// A lazy shader program factory.
class ProgramBuilder final {
    struct SlangEntryPointDesc {
        std::string name;
    };

    struct SlangModuleDesc {
        std::filesystem::path path;

        /// A list of entry points to be extracted in this module.
        kira::SmallVector<SlangEntryPointDesc> entryPointDescList;
    };

public:
    ///
    ProgramBuilder() = default;

    ///
    ~ProgramBuilder() = default;

public:
    ///
    ProgramBuilder &addSlangModuleFromPath(std::filesystem::path path);

#if 0
    ///
    ProgramBuilder &addSlangModuleFromSource(std::string const &source);
#endif

    /// Add an entry point found in the last added module.
    ///
    /// \exception kira::Anyhow if the entry point is not found in the last added module or there is
    /// no module available.
    ProgramBuilder &addEntryPoint(std::string_view name);

    /// Create a \c gfx::IShaderProgram from the added modules and entry points.
    Ref<Program> link(ComPtr<gfx::IDevice> const &device);

private:
    ///
    kira::SmallVector<SlangModuleDesc> moduleDescList;
};
} // namespace krd
