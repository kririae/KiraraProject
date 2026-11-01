#pragma once

#include <optix_types.h>

#include <span>

#include "flux/Core/Object.h"

namespace flux {
/// \brief Owns the module of a shader and its direct callable program group.
///
/// The module borrows its OptiX device context, which must outlive it.
class OptixShaderModule final : private Noncopyable {
public:
    /// \brief Builds the module from \p ir, which \c OptixBSDFCompiler::compile returns.
    ///
    /// \throw std::invalid_argument If \p deviceContext is null.
    /// \throw kira::Anyhow If OptiX rejects \p ir.
    OptixShaderModule(OptixDeviceContext deviceContext, std::span<char const> ir);

    ~OptixShaderModule();

    [[nodiscard]] OptixProgramGroup getProgramGroup() const noexcept { return program_; }

private:
    struct EmptyState {};

    explicit OptixShaderModule(EmptyState) noexcept {}

    OptixModule module_{};
    OptixProgramGroup program_{};
};
} // namespace flux
