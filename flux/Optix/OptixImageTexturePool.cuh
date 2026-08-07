#pragma once

#include <cuda_runtime.h>

#include "flux/Optix/OptixImageTexturePool.h"

namespace flux {
KIRA_DEVICE inline Vec4f OptixImageTexture::sample(Vec2f uv) const noexcept {
    if (componentCount == 1)
        return {tex2D<float>(texture, uv.x(), uv.y()), 0.0F, 0.0F, 0.0F};
    if (componentCount == 2) {
        auto const value = tex2D<float2>(texture, uv.x(), uv.y());
        return {value.x, value.y, 0.0F, 0.0F};
    }
    auto const value = tex2D<float4>(texture, uv.x(), uv.y());
    return {value.x, value.y, value.z, value.w};
}
} // namespace flux
