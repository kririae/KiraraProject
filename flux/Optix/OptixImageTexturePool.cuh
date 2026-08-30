#pragma once

#include <cuda_runtime.h>

#include "flux/Optix/OptixImageTexturePool.h"

namespace flux {
KIRA_DEVICE inline Vec4f
OptixImageTexture::sample(Vec2f uv, Vec2f const &duvdx, Vec2f const &duvdy) const noexcept {
    return sample(texture, uv, duvdx, duvdy);
}

KIRA_DEVICE inline Vec4f OptixImageTexture::samplePoint(Vec2f uv) const noexcept {
    return sample(pointTexture, uv);
}

KIRA_DEVICE inline Vec4f
OptixImageTexture::sample(cudaTextureObject_t textureObject, Vec2f uv) const noexcept {
    if (componentCount == 1)
        return {tex2D<float>(textureObject, uv.x(), uv.y()), 0.0F, 0.0F, 0.0F};
    if (componentCount == 2) {
        auto const value = tex2D<float2>(textureObject, uv.x(), uv.y());
        return {value.x, value.y, 0.0F, 0.0F};
    }
    auto const value = tex2D<float4>(textureObject, uv.x(), uv.y());
    return {value.x, value.y, value.z, value.w};
}

KIRA_DEVICE inline Vec4f OptixImageTexture::sample(
    cudaTextureObject_t textureObject, Vec2f uv, Vec2f const &duvdx, Vec2f const &duvdy
) const noexcept {
    auto const dx = make_float2(duvdx.x(), duvdx.y());
    auto const dy = make_float2(duvdy.x(), duvdy.y());
    auto const value = tex2DGrad<float4>(textureObject, uv.x(), uv.y(), dx, dy);
    return {value.x, value.y, value.z, value.w};
}
} // namespace flux
