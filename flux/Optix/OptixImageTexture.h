#pragma once

#include <cuda_runtime_api.h>

#include <cstdint>
#include <type_traits>

#include "flux/Scene/ImageAsset.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Samples one image texture with CUDA.
struct OptixImageTexture {
    /// CUDA texture object using the ImageTexture filter mode.
    cudaTextureObject_t texture;

    /// CUDA texture object using point filtering.
    cudaTextureObject_t pointTexture;

    /// Component mapping applied to a sampled texel.
    ImageComponentMapping componentMapping;

    /// Factor that multiplies the component mapping of a texel.
    Vec4f scale;

    /// Term that \c apply adds after the scale.
    Vec4f bias;

    /// Number of components stored in the CUDA array.
    std::uint8_t componentCount;

    /// \brief Returns the value of \p texel: its component mapping, times \c scale, plus \c bias.
    [[nodiscard]] KIRA_HOST_DEVICE Vec4f apply(Vec4f const &texel) const noexcept {
        return componentMapping.apply(texel) * scale + bias;
    }

    /// \brief Samples stored components before \c apply.
    [[nodiscard]] KIRA_DEVICE Vec4f
    sample(Vec2f uv, Vec2f const &duvdx, Vec2f const &duvdy) const noexcept;

    /// \brief Samples stored components with point filtering.
    [[nodiscard]] KIRA_DEVICE Vec4f samplePoint(Vec2f uv) const noexcept;

private:
    [[nodiscard]] KIRA_DEVICE Vec4f
    sample(cudaTextureObject_t textureObject, Vec2f uv) const noexcept;

    [[nodiscard]] KIRA_DEVICE Vec4f sample(
        cudaTextureObject_t textureObject, Vec2f uv, Vec2f const &duvdx, Vec2f const &duvdy
    ) const noexcept;
};

/// \brief Image textures by image-texture index, as device programs read them.
struct OptixImageTextureTable {
    /// Textures by image-texture index. An unused index holds a default texture.
    OptixImageTexture const *textures{};

    /// \brief Returns the texture at image-texture \p index.
    /// \pre \p index addresses an image texture of the scene.
    [[nodiscard]] KIRA_DEVICE OptixImageTexture const &
    getTexture(std::uint32_t index) const noexcept {
        return textures[index];
    }
};

static_assert(std::is_standard_layout_v<OptixImageTexture>);
static_assert(std::is_trivially_copyable_v<OptixImageTexture>);
static_assert(std::is_standard_layout_v<OptixImageTextureTable>);
static_assert(std::is_trivially_copyable_v<OptixImageTextureTable>);
} // namespace flux
