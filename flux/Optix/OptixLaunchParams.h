#pragma once

#include <cstdint>
#include <type_traits>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixContext.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

namespace flux {
/// \brief Pixel and sequence index assigned to one ray-generation invocation.
struct OptixLaunchSample {
    /// Pixel receiving this sample.
    Vec2u pixel;

    /// Absolute index in the pixel's sample sequence.
    std::uint64_t sampleIndex;
};

/// \brief Parameters shared by an OptiX render launch.
struct OptixLaunchParams {
    /// Borrowed OptiX scene view used by this launch.
    OptixContext::Impl scene;

    Camera::Impl camera;
    Sampler::Impl sampler;
    PathIntegrator::Impl integrator;

    /// Samples accumulated before this batch.
    std::uint64_t accumulatedSamples;

    /// Number of sequence elements skipped before sample zero.
    std::uint64_t sampleOffset;

    Film::Impl film;

    /// Number of samples assigned to each pixel in this batch.
    std::uint32_t batchSize;

public:
    /// \brief Decodes a linear OptiX launch index.
    ///
    /// Consecutive indices belong to consecutive samples of the same pixel.
    /// This keeps a full 32-sample batch on one warp in a one-dimensional
    /// launch.
    ///
    /// \pre \c batchSize and the film dimensions are nonzero.
    /// \pre \p launchIndex is smaller than
    /// `film.width * film.height * batchSize`.
    [[nodiscard]] KIRA_DEVICE OptixLaunchSample
    getLaunchSample(std::uint32_t launchIndex) const noexcept {
        auto const batchIndex = launchIndex % batchSize;
        auto const pixelIndex = launchIndex / batchSize;
        return {
            .pixel = {pixelIndex % film.width, pixelIndex / film.width},
            .sampleIndex = accumulatedSamples + sampleOffset + batchIndex,
        };
    }

    /// \brief Returns the contribution weight for one sample in this batch.
    ///
    /// \pre `accumulatedSamples + batchSize` is nonzero and representable.
    [[nodiscard]] KIRA_DEVICE float getSampleWeight() const noexcept {
        return 1.0F / static_cast<float>(accumulatedSamples + batchSize);
    }
};

static_assert(std::is_standard_layout_v<OptixLaunchSample>);
static_assert(std::is_trivially_copyable_v<OptixLaunchSample>);
static_assert(std::is_standard_layout_v<OptixLaunchParams>);
static_assert(std::is_trivially_copyable_v<OptixLaunchParams>);
} // namespace flux
