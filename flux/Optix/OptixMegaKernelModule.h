#pragma once

#include <optix_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include "flux/Core/Object.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Geometry.h"
#include "flux/Shading/BSDF.h"

namespace flux {
class Context;

/// Register budget the megakernel module is compiled against. Also bounds how many threads the
/// device keeps resident, which is what sizes a persistent launch.
inline constexpr unsigned int megakernelMaxRegisterCount = 128;

/// \brief Values specialized while compiling an OptiX program.
struct OptixProgramSpec {
    SamplerType samplerType;
    BSDFTypeMask bsdfTypes;
    bool shaderReorder;
    bool hasEnvMap;
    std::uint32_t chunkSize;

    [[nodiscard]] bool operator==(OptixProgramSpec const &) const = default;
};

/// \brief Owns the megakernel module and its program groups.
///
/// The module borrows its OptiX device context, which must outlive it.
class OptixMegaKernelModule final : private Noncopyable {
public:
    /// \brief Builds the module from \p modulePath, specialized by \p spec.
    ///
    /// The module must provide \c __raygen__megakernel and \c __miss__megakernel.
    OptixMegaKernelModule(
        OptixDeviceContext deviceContext, std::filesystem::path const &modulePath,
        OptixProgramSpec spec
    );

    /// \brief Returns the specialization required by \p context.
    ///
    /// The active integrator and sampler must be present. Only BSDFs referenced
    /// by visible primitives contribute to the specialization.
    [[nodiscard]] static OptixProgramSpec makeSpec(Context const &context);

    ~OptixMegaKernelModule();

    [[nodiscard]] OptixProgramSpec const &getSpec() const noexcept { return spec_; }

    /// \brief Returns every group of the module.
    [[nodiscard]] std::span<OptixProgramGroup const> getProgramGroups() const noexcept {
        return programs_;
    }

    [[nodiscard]] OptixProgramGroup getRaygenProgram() const noexcept {
        return programs_[raygenProgram];
    }

    [[nodiscard]] OptixProgramGroup getMissProgram() const noexcept {
        return programs_[missProgram];
    }

    [[nodiscard]] OptixProgramGroup getHitgroupProgram(GeometryType geometry) const noexcept {
        return programs_[hitgroupPrograms + static_cast<std::size_t>(geometry)];
    }

private:
    static constexpr std::size_t raygenProgram = 0;
    static constexpr std::size_t missProgram = raygenProgram + 1;
    static constexpr std::size_t hitgroupPrograms = missProgram + 1;
    static constexpr std::size_t numPrograms =
        hitgroupPrograms + static_cast<std::size_t>(GeometryType::Count);

    struct EmptyState {};

    OptixMegaKernelModule(
        EmptyState, OptixDeviceContext deviceContext, OptixProgramSpec spec
    ) noexcept;

    void buildModule(std::filesystem::path const &modulePath);
    void buildProgramGroups();

    OptixDeviceContext deviceContext_;
    OptixProgramSpec spec_;
    OptixModule module_{};
    std::array<OptixProgramGroup, numPrograms> programs_{};
};
} // namespace flux
