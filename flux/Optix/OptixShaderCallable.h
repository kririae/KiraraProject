#pragma once

/// \file flux/Optix/OptixShaderCallable.h
/// \brief The ABI between the megakernel and the direct callables of a shader.
///
/// Includes only headers that NVRTC compiles: no kira and no standard library header.

#ifndef __CUDACC_RTC__
#include <vector_types.h>
#endif

namespace flux {
/// \brief The shading context that a callable receives.
struct OptixShadingContext {
    float2 uv;
    float2 duvdx;
    float2 duvdy;

    /// The shading normal, which determines the shading frame as in \c Frame.
    float3 normal;

    /// The unit vector orthogonal to \c normal in which \c uv.x grows.
    float3 tangent;

    /// The normal that Geometry defines.
    float3 geometricNormal;

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

/// \brief The number of 32-bit words that a call passes in registers, as its parameters in order
/// fill them. A word past them goes through local memory.
inline constexpr unsigned int callRegisterWords = 11;

/// \brief The number of 32-bit words that the parameters of the function type \p Function occupy.
template <typename Function> struct ParameterWords;

template <typename Result, typename... Parameters> struct ParameterWords<Result(Parameters...)> {
    static constexpr unsigned int value = (0U + ... + ((sizeof(Parameters) + 3U) / 4U));
};

/// \brief Evaluates the EDF whose parameter words start at \p wordOffset, and returns the radiance
/// that it emits toward \p wo.
///
/// \p textureOffset, \p uv, and \p normal are the fields of \c OptixShadingContext of those names.
/// \p normal and \p wo are in world space, and \p wo points away from the surface. The tangent is
/// that of \c Frame around \p normal.
using OptixEDFCallable = float3(
    unsigned int wordOffset, unsigned int textureOffset, float2 uv, float3 normal, float3 wo
);

static_assert(ParameterWords<OptixEDFCallable>::value <= callRegisterWords);

static_assert(
    sizeof(OptixShadingContext) == 3 * sizeof(float2) + 3 * sizeof(float3) + sizeof(unsigned int)
);
static_assert(__is_standard_layout(OptixShadingContext));
static_assert(__is_trivially_copyable(OptixShadingContext));
static_assert(__is_standard_layout(OptixBSDFResult));
static_assert(__is_trivially_copyable(OptixBSDFResult));
} // namespace flux
