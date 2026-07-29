#pragma once

#include <optix_types.h>

#include <array>
#include <filesystem>

#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "flux/Optix/OptixSbt.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Shading/BSDF.h"

namespace flux {
class Context;

/// \brief Values specialized while compiling an OptiX program.
struct OptixProgramSpec {
    SamplerType samplerType; // (1)

    [[nodiscard]] bool operator==(OptixProgramSpec const &) const = default;
};

/// \brief Owns the module, program groups, and pipeline for an OptiX scene.
///
/// The program borrows its OptiX device context, which must outlive it.
class OptixProgram final : private Noncopyable {
public:
    /// \brief Builds a triangle-intersection pipeline from \p modulePath.
    ///
    /// The module must provide \c __raygen__megakernel, the radiance and shadow
    /// miss programs, and the diffuse and shadow triangle hit programs.
    OptixProgram(
        OptixDeviceContext deviceContext, std::filesystem::path const &modulePath,
        OptixProgramSpec spec
    );

    /// \brief Returns the specialization required by \p context.
    ///
    /// The specialization uses the active integrator and sampler. Both must be
    /// present.
    [[nodiscard]] static OptixProgramSpec makeSpec(Context const &context);

    ~OptixProgram();

    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }
    [[nodiscard]] OptixProgramSpec const &getSpec() const noexcept { return spec_; }
    [[nodiscard]] OptixProgramGroup getRaygenProgram() const noexcept { return raygenProgram_; }

    [[nodiscard]] OptixProgramGroup getMissProgram(RayType ray) const noexcept {
        return missPrograms_[static_cast<std::size_t>(ray)];
    }

    [[nodiscard]] OptixProgramGroup
    getRadianceHitgroupProgram(BSDFType bsdf, GeometryType geometry) const noexcept {
        return radianceHitgroupPrograms_[OptixSbt::getHitgroupBlock(bsdf, geometry)];
    }

    [[nodiscard]] OptixProgramGroup getShadowHitgroupProgram(GeometryType geometry) const noexcept {
        return shadowHitgroupPrograms_[static_cast<std::size_t>(geometry)];
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
    std::array<OptixProgramGroup, static_cast<std::size_t>(RayType::Count)> missPrograms_{};
    std::array<
        OptixProgramGroup,
        static_cast<std::size_t>(BSDFType::Count) * static_cast<std::size_t>(GeometryType::Count)>
        radianceHitgroupPrograms_{};
    std::array<OptixProgramGroup, static_cast<std::size_t>(GeometryType::Count)>
        shadowHitgroupPrograms_{};
    OptixPipeline pipeline_{};
};
} // namespace flux
