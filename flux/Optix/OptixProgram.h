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
    /// \param shaderModules In slot order: the SBT holds their callables in this order.
    /// \pre \p megaKernelModule and each of \p shaderModules are not null.
    OptixProgram(
        OptixDeviceContext deviceContext,
        std::shared_ptr<OptixMegaKernelModule const> megaKernelModule,
        std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules
    );

    ~OptixProgram();

    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }

    /// \brief Returns the shader modules, in slot order.
    [[nodiscard]] std::span<std::shared_ptr<OptixShaderModule const> const>
    getShaderModules() const noexcept {
        return shaderModules_;
    }

private:
    struct EmptyState {};

    OptixProgram(
        EmptyState, OptixDeviceContext deviceContext,
        std::shared_ptr<OptixMegaKernelModule const> megaKernelModule,
        std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules
    ) noexcept;

    void buildPipeline();

    OptixDeviceContext deviceContext_;
    std::shared_ptr<OptixMegaKernelModule const> megaKernelModule_;
    std::vector<std::shared_ptr<OptixShaderModule const>> shaderModules_;
    OptixPipeline pipeline_{};
};
} // namespace flux
