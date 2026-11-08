#pragma once

#include <optix_types.h>

#include <memory>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixMegaKernelModule.h"
#include "flux/Optix/OptixShaderModule.h"

namespace flux {
/// \brief Owns the OptiX pipeline of its modules.
///
/// The program borrows its OptiX device context, which must outlive it.
class OptixProgram final : private Noncopyable {
public:
    /// \brief Links the groups of the modules into a pipeline.
    ///
    /// The megakernel calls the shaders from raygen.
    /// \param shaderModules In slot order: the SBT holds their BSDF callables in this order.
    /// \param edfModules In EDF slot order: the SBT holds their EDF callables in this order, after
    ///        the BSDF callables.
    /// \pre \p megaKernelModule, each of \p shaderModules, and each of \p edfModules are not null.
    OptixProgram(
        OptixDeviceContext deviceContext,
        std::shared_ptr<OptixMegaKernelModule const> megaKernelModule,
        std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules,
        std::vector<std::shared_ptr<OptixShaderModule const>> edfModules
    );

    ~OptixProgram();

    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }

    /// \brief Returns the modules whose BSDF callables the program links, in slot order.
    [[nodiscard]] std::span<std::shared_ptr<OptixShaderModule const> const>
    getShaderModules() const noexcept {
        return shaderModules_;
    }

    /// \brief Returns the modules whose EDF callables the program links, in EDF slot order.
    [[nodiscard]] std::span<std::shared_ptr<OptixShaderModule const> const>
    getEDFModules() const noexcept {
        return edfModules_;
    }

private:
    struct EmptyState {};

    OptixProgram(
        EmptyState, OptixDeviceContext deviceContext,
        std::shared_ptr<OptixMegaKernelModule const> megaKernelModule,
        std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules,
        std::vector<std::shared_ptr<OptixShaderModule const>> edfModules
    ) noexcept;

    void buildPipeline();

    OptixDeviceContext deviceContext_;
    std::shared_ptr<OptixMegaKernelModule const> megaKernelModule_;
    std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules_;
    std::vector<std::shared_ptr<OptixShaderModule const>> edfModules_;
    OptixPipeline pipeline_{};
};
} // namespace flux
