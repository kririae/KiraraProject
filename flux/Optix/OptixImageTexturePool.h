#pragma once

#include <cuda_runtime_api.h>

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Scene/ImageAsset.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief CUDA texture object for one ImageTexture.
struct OptixImageTexture {
    cudaTextureObject_t texture;
    ImageComponentMapping componentMapping;
    std::uint8_t componentCount;

    /// \brief Samples stored components before component mapping.
    [[nodiscard]] KIRA_DEVICE Vec4f sample(Vec2f uv) const noexcept;
};

/// \brief Owns CUDA arrays and texture objects used by OptiX.
///
/// Image textures that share an ImageAsset also share one CUDA array.
class OptixImageTexturePool final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Device view valid until the next \c build or destruction.
    struct Impl {
        OptixImageTexture const *textures;

        /// \brief Returns texture \p index.
        /// \pre \p index refers to an entry in \c textures.
        [[nodiscard]] KIRA_DEVICE OptixImageTexture const &get(std::uint32_t index) const noexcept {
            return textures[index];
        }
    };

    explicit OptixImageTexturePool(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), deviceTextures_(stream) {}
    ~OptixImageTexturePool() noexcept;

    /// \brief Rebuilds entries in the order of \p textures.
    ///
    /// The order must match ImageTexture::Impl::imageTextureIndex. A failure
    /// leaves the pool valid for destruction or another build, but not launch.
    void build(std::span<Ref<ImageTexture const> const> textures);

    /// \brief Returns a device view valid until the next \c build.
    [[nodiscard]] Impl getImpl() const noexcept { return {.textures = deviceTextures_.data()}; }

private:
    void clear() noexcept;

    std::vector<cudaArray_t> arrays_;
    std::vector<cudaTextureObject_t> textureObjects_;
    std::vector<OptixImageTexture> staging_;
    DeviceBuffer<OptixImageTexture> deviceTextures_;
};

static_assert(std::is_standard_layout_v<OptixImageTexture>);
static_assert(std::is_trivially_copyable_v<OptixImageTexture>);
static_assert(std::is_standard_layout_v<OptixImageTexturePool::Impl>);
static_assert(std::is_trivially_copyable_v<OptixImageTexturePool::Impl>);
} // namespace flux
