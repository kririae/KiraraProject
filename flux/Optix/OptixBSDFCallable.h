#pragma once

/// \file flux/Optix/OptixBSDFCallable.h
/// \brief The ABI between the megakernel and the direct callable of a BSDF.
///
/// Includes only headers that NVRTC compiles: no kira and no standard library header.

#ifndef __CUDACC_RTC__
#include <vector_types.h>
#endif

namespace flux {
/// \brief The shading context that a callable receives.
struct OptixShadingContext {
    float3 position;

    /// The shading normal, which determines the shading frame as in \c Frame.
    float3 normal;

    float2 uv;
    float2 duvdx;
    float2 duvdy;

    /// The index of the first texture of the BSDF in \c OptixCallableLaunchParams::textureIndices.
    unsigned int textureOffset;
};

/// \brief A \c BSDFResult.
struct OptixBSDFResult {
    struct {
        float3 value;
        float pdf;
    } evaluation;

    struct {
        float3 weight;
        float3 wi;
        float pdf;
        float eta;

        /// A \c BSDFLobe.
        unsigned int lobe;
    } sample;
};

/// \brief Executes the BSDF whose parameter words start at \p wordOffset.
///
/// Directions are shading-local.
using OptixBSDFCallable = OptixBSDFResult(
    unsigned int wordOffset, OptixShadingContext context, float3 wo, float3 wi, bool eval, float u1,
    float2 u2
);

static_assert(__is_standard_layout(OptixShadingContext));
static_assert(__is_trivially_copyable(OptixShadingContext));
static_assert(__is_standard_layout(OptixBSDFResult));
static_assert(__is_trivially_copyable(OptixBSDFResult));
} // namespace flux
