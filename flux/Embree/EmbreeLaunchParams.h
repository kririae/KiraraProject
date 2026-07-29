#pragma once

#include <cstdint>

#include "flux/Embree/EmbreeContext.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

namespace flux {
/// \brief Immutable state shared by one Embree render.
struct EmbreeLaunchParams {
    /// Persistent Embree scene used by this render.
    EmbreeContext::Impl scene;

    /// Launch-time camera.
    Camera::Impl camera;

    /// Launch-time sampler dispatcher.
    Sampler::Impl sampler;

    /// Samples accumulated before this batch.
    std::uint64_t accumulatedSamples;

    /// Number of sequence elements skipped before sample zero.
    std::uint64_t sampleOffset;

    /// Launch-time output channels.
    Film::Impl film;

    /// Number of samples assigned to each pixel in this batch.
    std::uint32_t batchSize;

public:
    /// \brief Returns the absolute sequence index for one batch element.
    ///
    /// \pre \p batchIndex is less than \c batchSize.
    [[nodiscard]] std::uint64_t getSampleIndex(std::uint32_t batchIndex) const noexcept {
        return accumulatedSamples + sampleOffset + batchIndex;
    }

    /// \brief Returns the contribution weight for one sample in this batch.
    [[nodiscard]] float getSampleWeight() const noexcept {
        return 1.0F / static_cast<float>(accumulatedSamples + batchSize);
    }

    /// \brief Returns the weight retained from previous batches.
    [[nodiscard]] float getAccumulatedWeight() const noexcept {
        return static_cast<float>(accumulatedSamples) /
               static_cast<float>(accumulatedSamples + batchSize);
    }
};

} // namespace flux
