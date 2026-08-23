#pragma once

#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Scene/LightTableData.h"

namespace flux {
/// \brief Owns the light table and power distribution used by Embree.
class EmbreeLightSampler final : private Noncopyable {
public:
    struct Impl;

    /// \brief Rebuilds the sampler and assigns light indices in \p primImpls.
    void build(
        std::span<Ref<Light const> const> lights, std::span<Ref<Primitive const> const> prims,
        std::span<Primitive::Impl> primImpls
    );

    void clear() noexcept;

    /// \brief Returns the sampler used for rendering.
    ///
    /// The result remains valid until the next \c build or \c clear.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    LightTableData tableData_;
    std::vector<float> powerCDF_;
};

/// \brief Embree light table and selection distribution used during rendering.
struct EmbreeLightSampler::Impl {
    LightTable table{};
    LightPowerDistribution power{};

public:
    [[nodiscard]] SampledLight sample(LightSamplingContext const &ctx, float u) const noexcept {
        (void)ctx;
        return power.sample(u);
    }

    [[nodiscard]] float
    pmf(LightSamplingContext const &ctx, std::uint32_t lightIndex) const noexcept {
        (void)ctx;
        return power.pmf(lightIndex);
    }
};

static_assert(std::is_standard_layout_v<EmbreeLightSampler::Impl>);
static_assert(std::is_trivially_copyable_v<EmbreeLightSampler::Impl>);
} // namespace flux
