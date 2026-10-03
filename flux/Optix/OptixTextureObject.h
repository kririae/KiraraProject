#pragma once

#include <cuda_runtime_api.h>

#include <utility>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixImagePool.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Owns one CUDA texture object.
class CudaTextureObject final : private Noncopyable {
public:
    CudaTextureObject() noexcept = default;
    explicit CudaTextureObject(cudaTextureObject_t handle) noexcept : handle_(handle) {}
    CudaTextureObject(CudaTextureObject &&other) noexcept
        : handle_(std::exchange(other.handle_, cudaTextureObject_t{})) {}
    CudaTextureObject &operator=(CudaTextureObject &&other) noexcept {
        std::swap(handle_, other.handle_);
        return *this;
    }
    ~CudaTextureObject() {
        if (handle_)
            cudaCheck<false>(cudaDestroyTextureObject(handle_));
    }

    [[nodiscard]] cudaTextureObject_t get() const noexcept { return handle_; }

private:
    cudaTextureObject_t handle_{};
};

/// \brief Creates a texture object that samples \p image with \p addressMode and \p filterMode.
///
/// The texture object reads \p image when a launch samples it, so the image must outlive it.
/// \throw kira::Anyhow If CUDA fails.
[[nodiscard]] CudaTextureObject createTextureObject(
    OptixImage const &image, ImageTextureAddressMode addressMode, ImageTextureFilterMode filterMode
);
} // namespace flux
