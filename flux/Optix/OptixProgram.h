#pragma once

#include <optix_types.h>

#include <filesystem>

#include "flux/Core/Object.h"

namespace flux {
/// \brief Owns the module, program groups, and pipeline for an OptiX scene.
class OptixProgram final : private Noncopyable {
public:
    /// \brief Builds a triangle-intersection pipeline from \p modulePath.
    ///
    /// The module must provide \c __raygen__megakernel,
    /// \c __miss__radiance, and \c __closesthit__triangle.
    /// \param deviceContext OptiX context used to create the program.
    /// \param modulePath Path to the OptiX IR module.
    /// \throw kira::Anyhow If the module cannot be read or OptiX setup fails.
    OptixProgram(OptixDeviceContext deviceContext, std::filesystem::path const &modulePath);

    /// \brief Releases the pipeline, program groups, and module.
    ~OptixProgram();

    /// \brief Returns the linked pipeline.
    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }

    /// \brief Returns the ray-generation program group.
    [[nodiscard]] OptixProgramGroup getRaygenProgram() const noexcept { return raygenProgram_; }

    /// \brief Returns the miss program group.
    [[nodiscard]] OptixProgramGroup getMissProgram() const noexcept { return missProgram_; }

    /// \brief Returns the triangle hit-group program.
    [[nodiscard]] OptixProgramGroup getHitgroupProgram() const noexcept { return hitgroupProgram_; }

private:
    void buildModule(std::filesystem::path const &modulePath);
    void buildProgramGroups();
    void buildPipeline();
    void reset() noexcept;

    OptixDeviceContext deviceContext_;
    OptixModule module_{};
    OptixProgramGroup raygenProgram_{};
    OptixProgramGroup missProgram_{};
    OptixProgramGroup hitgroupProgram_{};
    OptixPipeline pipeline_{};
};
} // namespace flux
