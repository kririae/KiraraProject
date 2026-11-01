#pragma once

#include <optix_types.h>

#include <memory>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixMegaKernelModule.h"

namespace flux {
/// \brief Owns the OptiX pipeline of its modules.
///
/// The program borrows its OptiX device context, which must outlive it.
class OptixProgram final : private Noncopyable {
public:
    /// \brief Links the groups of the modules into a pipeline.
    /// \pre \p megaKernelModule is not null.
    OptixProgram(
        OptixDeviceContext deviceContext,
        std::shared_ptr<OptixMegaKernelModule const> megaKernelModule
    );

    ~OptixProgram();

    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }

private:
    struct EmptyState {};

    OptixProgram(
        EmptyState, OptixDeviceContext deviceContext,
        std::shared_ptr<OptixMegaKernelModule const> megaKernelModule
    ) noexcept;

    void buildPipeline();

    OptixDeviceContext deviceContext_;
    std::shared_ptr<OptixMegaKernelModule const> megaKernelModule_;
    OptixPipeline pipeline_{};
};
} // namespace flux
