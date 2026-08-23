#pragma once

#include <optix_types.h>

#include <array>
#include <filesystem>

#include "flux/Core/Object.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Geometry.h"
#include "flux/Shading/BSDF.h"

namespace flux {
class Context;

/// \brief Values specialized while compiling an OptiX program.
struct OptixProgramSpec {
    SamplerType samplerType; // (1)
    BSDFTypeMask bsdfTypes;  // (2)
    bool shaderReorder;      // (3)

    [[nodiscard]] bool operator==(OptixProgramSpec const &) const = default;
};

/// \brief Owns the module, program groups, and pipeline for an OptiX scene.
///
/// The program borrows its OptiX device context, which must outlive it.
class OptixProgram final : private Noncopyable {
public:
    /// \brief Builds a triangle-intersection pipeline from \p modulePath.
    ///
    /// The module must provide \c __raygen__megakernel and \c __miss__megakernel.
    OptixProgram(
        OptixDeviceContext deviceContext, std::filesystem::path const &modulePath,
        OptixProgramSpec spec
    );

    /// \brief Returns the specialization required by \p context.
    ///
    /// The active integrator and sampler must be present. Only BSDFs referenced
    /// by visible primitives contribute to the specialization.
    [[nodiscard]] static OptixProgramSpec makeSpec(Context const &context);

    ~OptixProgram();

    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }
    [[nodiscard]] OptixProgramSpec const &getSpec() const noexcept { return spec_; }
    [[nodiscard]] OptixProgramGroup getRaygenProgram() const noexcept { return raygenProgram_; }
    [[nodiscard]] OptixProgramGroup getMissProgram() const noexcept { return missProgram_; }

    [[nodiscard]] OptixProgramGroup getHitgroupProgram(GeometryType geometry) const noexcept {
        return hitgroupPrograms_[static_cast<std::size_t>(geometry)];
    }

private:
    void buildModule(std::filesystem::path const &modulePath);
    void buildProgramGroups();
    void buildPipeline();
    void reset() noexcept;

    OptixDeviceContext deviceContext_;
    OptixProgramSpec spec_;
    OptixModule module_{};
    OptixProgramGroup raygenProgram_{};
    OptixProgramGroup missProgram_{};
    std::array<OptixProgramGroup, static_cast<std::size_t>(GeometryType::Count)>
        hitgroupPrograms_{};
    OptixPipeline pipeline_{};
};
} // namespace flux
