#pragma once

/// \file flux/Optix/OptixCallableLaunchParams.h
/// \brief The part of the launch parameters that callable modules read too.
///
/// Includes only headers that NVRTC compiles: no kira and no standard library header.

#include "flux/Optix/OptixImageTexture.h"

namespace flux {
/// \brief The start of the launch parameters.
///
/// Its layout is the ABI between the megakernel and the callables.
struct OptixCallableLaunchParams {
    /// Image textures by image-texture index.
    OptixImageTextureTable imageTextures;

    /// Parameter words of the callables, each at its BSDF's word offset.
    unsigned int const *words{};

    /// The image-texture index of each texture of the callables, starting at each BSDF's texture
    /// offset, in slot order.
    unsigned int const *textureIndices{};
};

static_assert(__is_standard_layout(OptixCallableLaunchParams));
static_assert(__is_trivially_copyable(OptixCallableLaunchParams));
} // namespace flux
