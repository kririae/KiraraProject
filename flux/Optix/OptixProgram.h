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
    /// Sampler implementation used by the pipeline.
    SamplerType samplerType; // (1)

    /// \brief Compares all specialization values.
    [[nodiscard]] bool operator==(OptixProgramSpec const &) const = default;
};

/// \brief Owns the module, program groups, and pipeline for an OptiX scene.
class OptixProgram final : private Noncopyable {
public:
    /// \brief Builds a triangle-intersection pipeline from \p modulePath.
    ///
    /// The module must provide \c __raygen__megakernel, the radiance and shadow
    /// miss programs, and the diffuse and shadow triangle hit programs.
    /// \param deviceContext OptiX context used to create the program.
    /// \param modulePath Path to the OptiX IR module.
    /// \param spec Values specialized into the OptiX module.
    /// \throw kira::Anyhow If the module cannot be read or OptiX setup fails.
    OptixProgram(
        OptixDeviceContext deviceContext, std::filesystem::path const &modulePath,
        OptixProgramSpec spec
    );

    /// \brief Returns the specialization implied by \p context.
    ///
    /// \param context Host scene whose pipeline specialization is requested.
    /// \throw kira::Anyhow If \p context has no active integrator or sampler.
    [[nodiscard]] static OptixProgramSpec makeSpec(Context const &context);

    /// \brief Releases the pipeline, program groups, and module.
    ~OptixProgram();

    /// \brief Returns the linked pipeline.
    [[nodiscard]] OptixPipeline getPipeline() const noexcept { return pipeline_; }

    /// \brief Returns the values specialized into this program.
    [[nodiscard]] OptixProgramSpec const &getSpec() const noexcept { return spec_; }

    /// \brief Returns the ray-generation program group.
    [[nodiscard]] OptixProgramGroup getRaygenProgram() const noexcept { return raygenProgram_; }

    /// \brief Returns the miss program for \p ray.
    [[nodiscard]] OptixProgramGroup getMissProgram(RayType ray) const noexcept {
        return missPrograms_[static_cast<std::size_t>(ray)];
    }

    /// \brief Returns the radiance hitgroup for a concrete program-type pair.
    [[nodiscard]] OptixProgramGroup
    getRadianceHitgroupProgram(BSDFType bsdf, GeometryType geometry) const noexcept {
        return radianceHitgroupPrograms_[OptixSbt::getHitgroupBlock(bsdf, geometry)];
    }

    /// \brief Returns the shadow hitgroup for \p geometry.
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
