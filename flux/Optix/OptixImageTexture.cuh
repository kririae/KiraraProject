#pragma once

/// \file flux/Optix/OptixImageTexture.cuh
/// \brief The device sampling of image textures.
///
/// Includes only headers that NVRTC compiles: no kira and no standard library header.

#include "flux/Optix/OptixImageTexture.h"

namespace flux {
__device__ inline float4
OptixImageTexture::sample(float2 uv, float2 duvdx, float2 duvdy) const noexcept {
    return tex2DGrad<float4>(texture, uv.x, uv.y, duvdx, duvdy);
}

__device__ inline float4 OptixImageTexture::samplePoint(float2 uv) const noexcept {
    auto value = float4{};
    if (componentCount == 1) {
        value.x = tex2D<float>(pointTexture, uv.x, uv.y);
    } else if (componentCount == 2) {
        auto const stored = tex2D<float2>(pointTexture, uv.x, uv.y);
        value.x = stored.x;
        value.y = stored.y;
    } else {
        value = tex2D<float4>(pointTexture, uv.x, uv.y);
    }
    return value;
}
} // namespace flux
