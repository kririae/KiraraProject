#pragma once

#include <cuda_runtime_api.h>

#include <cstdint>
#include <type_traits>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Scene/ImageAsset.h"
#include "flux/Shading/Texture.h"

namespace flux {
class Context;

/// \brief CUDA texture views for one ImageTexture.
struct OptixImageTexture {
    /// View using the ImageTexture filter mode.
    cudaTextureObject_t texture;

    /// View with point filtering.
    cudaTextureObject_t pointTexture;

    /// Mapping applied after sampling.
    ImageComponentMapping componentMapping;

    /// Number of components stored in the CUDA array.
    std::uint8_t componentCount;

    /// \brief Samples stored components before component mapping.
    [[nodiscard]] KIRA_DEVICE Vec4f sample(Vec2f uv) const noexcept;

    /// \brief Samples stored components with point filtering.
    [[nodiscard]] KIRA_DEVICE Vec4f samplePoint(Vec2f uv) const noexcept;

private:
    [[nodiscard]] KIRA_DEVICE Vec4f
    sample(cudaTextureObject_t textureObject, Vec2f uv) const noexcept;
};

/// \brief Owns CUDA arrays and texture objects used by OptiX.
///
/// Image textures that share an ImageAsset also share one CUDA array. Each
/// ImageTexture has its configured filter and a point-filtered view.
class OptixImageTexturePool final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Device view valid until the next \c build or destruction.
    struct Impl {
        OptixImageTexture const *textures{};

        /// \brief Returns texture \p index.
        /// \pre \p index refers to an entry in \c textures.
        [[nodiscard]] KIRA_DEVICE OptixImageTexture const &get(std::uint32_t index) const noexcept {
            return textures[index];
        }
    };

    explicit OptixImageTexturePool(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), deviceTextures_(stream) {}
    ~OptixImageTexturePool() noexcept;

    /// \brief Rebuilds the image texture entries in \p context.
    ///
    /// After this function throws, call \c build again before launch or destroy
    /// the pool.
    void build(Context const &context);

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
