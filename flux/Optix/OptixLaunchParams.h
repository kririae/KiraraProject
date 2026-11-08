#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Optix/OptixCallableLaunchParams.h"
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
    /// The part that callable modules read too.
    OptixCallableLaunchParams callable;

    /// OptiX scene for this launch.
    OptixContext::Impl scene;

    Camera::Impl camera;
    Sampler::Impl sampler;
    PathIntegrator::Impl integrator;
    BSDF::Dispatcher bsdfDispatcher;

    /// Samples accumulated before this batch.
    std::uint64_t accumulatedSamples;

    /// Number of sequence elements skipped before sample zero.
    std::uint64_t sampleOffset;

    /// Next chunk not yet claimed by any lane, counted from the chunks the launch covers.
    std::uint32_t *nextChunk;

    Film::Impl film;

    /// Number of samples assigned to each pixel in this batch.
    std::uint32_t batchSize;

    std::uint32_t pathCount;

    /// Consecutive paths in a chunk.
    std::uint32_t chunkSize;

    /// Number of chunks in the batch.
    std::uint32_t chunkCount;

    /// Whether radiance traversal uses shader execution reordering.
    bool shaderReorder;

    /// Whether the scene has an active environment map.
    bool hasEnvMap;

public:
    /// \brief Decodes a path index into the sample it renders.
    ///
    /// \pre The film dimensions are nonzero.
    /// \pre \p pathIndex is smaller than \c pathCount.
    [[nodiscard]] KIRA_DEVICE OptixLaunchSample
    getLaunchSample(std::uint32_t pathIndex) const noexcept {
        auto const pixelCount = film.width * film.height;
        auto const batchIndex = pathIndex / pixelCount;
        auto const pixelIndex = pathIndex % pixelCount;
        return {
            .pixel = {pixelIndex % film.width, pixelIndex / film.width},
            .sampleIndex = accumulatedSamples + sampleOffset + batchIndex,
        };
    }

    /// \brief Returns the first path of the chunk that a lane claims, or \c pathCount if none is
    /// left.
    ///
    /// \p claimed is the number of earlier claims.
    ///
    /// \pre \c chunkCount is \c pathCount over \p chunkSize, rounded up.
    /// \pre \p launchSize is at most \c chunkCount.
    [[nodiscard]] KIRA_DEVICE std::uint32_t getChunkBegin(
        std::uint32_t claimed, std::uint32_t launchSize, std::uint32_t chunkSize
    ) const noexcept {
        return claimed < chunkCount - launchSize ? (launchSize + claimed) * chunkSize : pathCount;
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
static_assert(offsetof(OptixLaunchParams, callable) == 0);
} // namespace flux
