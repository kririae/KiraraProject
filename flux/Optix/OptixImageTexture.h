#pragma once

/// \file flux/Optix/OptixImageTexture.h
/// \brief The layout of image textures that device programs sample.
///
/// Includes only headers that NVRTC compiles: no kira and no standard library header.

#ifndef __CUDACC_RTC__
#include <cuda_runtime_api.h>
#endif

#include "flux/Scene/ImageComponentMapping.h"

namespace flux {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

/// \brief Samples one image texture with CUDA.
struct OptixImageTexture {
    /// CUDA texture object using the ImageTexture filter mode.
    cudaTextureObject_t texture;

    /// CUDA texture object using point filtering.
    cudaTextureObject_t pointTexture;

    /// Component mapping applied to a sampled texel.
    ImageComponentMapping componentMapping;

    /// Number of components stored in the CUDA array.
    unsigned char componentCount;

    float4 scale;

    float4 bias;

    /// \brief Returns the value of \p texel.
    [[nodiscard]] __device__ float4 apply(float4 texel) const noexcept {
        auto const mapped = componentMapping.apply<float4>(texel.x, texel.y, texel.z, texel.w);
        return float4{
            mapped.x * scale.x + bias.x, mapped.y * scale.y + bias.y, mapped.z * scale.z + bias.z,
            mapped.w * scale.w + bias.w
        };
    }

    /// \brief Samples stored components with the ImageTexture filter mode, before \c apply.
    [[nodiscard]] __device__ float4 sample(float2 uv, float2 duvdx, float2 duvdy) const noexcept;

    /// \brief Samples stored components with point filtering, before \c apply.
    [[nodiscard]] __device__ float4 samplePoint(float2 uv) const noexcept;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

/// \brief Image textures by image-texture index, as device programs read them.
struct OptixImageTextureTable {
    /// Textures by image-texture index. An unused index holds a default texture.
    OptixImageTexture const *textures{};

    /// \brief Returns the texture at image-texture \p index.
    /// \pre \p index addresses an image texture of the scene.
    [[nodiscard]] __device__ OptixImageTexture const &
    getTexture(unsigned int index) const noexcept {
        return textures[index];
    }
};

static_assert(__is_standard_layout(OptixImageTexture));
static_assert(__is_trivially_copyable(OptixImageTexture));
static_assert(__is_standard_layout(OptixImageTextureTable));
static_assert(__is_trivially_copyable(OptixImageTextureTable));
} // namespace flux
