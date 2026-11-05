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
    CudaTextureObject(CudaTextureObject &&other) noexcept { swap(*this, other); }
    CudaTextureObject &operator=(CudaTextureObject &&other) noexcept {
        CudaTextureObject replacement(std::move(other));
        swap(*this, replacement);
        return *this;
    }
    ~CudaTextureObject() {
        if (handle_)
            cudaCheck<false>(cudaDestroyTextureObject(handle_));
    }

    [[nodiscard]] cudaTextureObject_t get() const noexcept { return handle_; }

    friend void swap(CudaTextureObject &lhs, CudaTextureObject &rhs) noexcept {
        using std::swap;
        swap(lhs.handle_, rhs.handle_);
    }

private:
    cudaTextureObject_t handle_{};
};

/// \brief Creates a texture object that samples \p image with \p addressModes and \p filterMode.
///
/// The texture object reads \p image when a launch samples it, so the image must outlive it.
/// \throw kira::Anyhow If CUDA fails.
[[nodiscard]] CudaTextureObject createTextureObject(
    OptixImage const &image, ImageTextureAddressModes const &addressModes,
    ImageTextureFilterMode filterMode
);
} // namespace flux
