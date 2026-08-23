#pragma once

#include <cuda_runtime_api.h>
#include <optix_stubs.h>

#include <chrono>
#include <source_location>
#include <utility>

#include "flux/Core/Logging.h"
#include "flux/Core/Object.h"
#include "kira/Anyhow.h"

namespace flux {
/// \brief Converts a CUDA allocation pointer to an OptiX device address.
/// \param pointer CUDA allocation pointer, or null.
/// \return The corresponding OptiX device address.
template <typename T> [[nodiscard]] CUdeviceptr devicePointer(T const *pointer) noexcept {
    return reinterpret_cast<CUdeviceptr>(pointer);
}

/// \brief Records the CUDA stream that orders an object's lifetime.
///
/// The mixin does not own or synchronize the stream. The caller must keep the
/// stream alive and establish any required ordering before changing it.
class CudaStreamMixin {
public:
    /// \brief Binds the object to the legacy default stream.
    CudaStreamMixin() noexcept = default;

    /// \brief Binds the object to \p stream.
    /// \param stream Stream that orders the object's lifetime.
    explicit CudaStreamMixin(cudaStream_t stream) noexcept : stream_(stream) {}

    /// \brief Returns the stream that currently orders the object's lifetime.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return stream_; }

    /// \brief Publishes \p stream as the new lifetime stream.
    ///
    /// This function does not add cross-stream synchronization.
    /// \param stream Stream to use for subsequent ordered operations.
    void setStream(cudaStream_t stream) noexcept { stream_ = stream; }

    friend void swap(CudaStreamMixin &lhs, CudaStreamMixin &rhs) noexcept {
        using std::swap;
        swap(lhs.stream_, rhs.stream_);
    }

private:
    cudaStream_t stream_{cudaStreamLegacy};
};

/// \brief Checks a CUDA Runtime API result.
///
/// \tparam ShouldThrow Whether an error should throw. Destructors pass
/// \c false so cleanup remains non-throwing.
/// \param result CUDA result to check.
/// \param location Call site reported on failure.
/// \throw kira::Anyhow if \p result is an error and \c ShouldThrow is \c true.
template <bool ShouldThrow = true>
inline void cudaCheck(
    cudaError_t result, std::source_location location = std::source_location::current()
) noexcept(!ShouldThrow) {
    if (result == cudaSuccess)
        return;

    auto const message = fmt::format(
        "cudaCheck(): CUDA API call error {} ({}): \"{}\" at {}:{}", static_cast<int>(result),
        cudaGetErrorName(result), cudaGetErrorString(result), location.file_name(), location.line()
    );
    if constexpr (ShouldThrow)
        throw kira::Anyhow("{}", message);
    else
        LogError("{}", message);
}

/// \brief Measures work ordered by a CUDA stream.
class CudaStreamTimer final : private Noncopyable, private CudaStreamMixin {
public:
    explicit CudaStreamTimer(cudaStream_t stream) : CudaStreamTimer(EmptyState{}, stream) {
        cudaCheck(cudaEventCreate(&start_));
        try {
            cudaCheck(cudaEventCreate(&stop_));
        } catch (...) {
            cudaCheck<false>(cudaEventDestroy(start_));
            throw;
        }
    }

    ~CudaStreamTimer() {
        if (stop_)
            cudaCheck<false>(cudaEventDestroy(stop_));
        if (start_)
            cudaCheck<false>(cudaEventDestroy(start_));
    }

    void start() { cudaCheck(cudaEventRecord(start_, getStream())); }

    /// \brief Waits for the measured work and returns its elapsed time.
    [[nodiscard]] std::chrono::nanoseconds stop() {
        cudaCheck(cudaEventRecord(stop_, getStream()));
        cudaCheck(cudaEventSynchronize(stop_));

        float milliseconds = 0.0F;
        cudaCheck(cudaEventElapsedTime(&milliseconds, start_, stop_));
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<float, std::milli>{milliseconds}
        );
    }

private:
    struct EmptyState {};

    CudaStreamTimer(EmptyState, cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    cudaEvent_t start_{};
    cudaEvent_t stop_{};
};

/// \brief Checks an OptiX API result.
///
/// \tparam ShouldThrow Whether an error should throw. Destructors pass
/// \c false so cleanup remains non-throwing.
/// \param result OptiX result to check.
/// \param location Call site reported on failure.
/// \throw kira::Anyhow if \p result is an error and \c ShouldThrow is \c true.
template <bool ShouldThrow = true>
inline void optixCheck(
    OptixResult result, std::source_location location = std::source_location::current()
) noexcept(!ShouldThrow) {
    if (result == OPTIX_SUCCESS)
        return;

    auto const *name = optixGetErrorName(result);
    auto const *description = optixGetErrorString(result);
    auto const message = fmt::format(
        "optixCheck(): OptiX API call error {} ({}): \"{}\" at {}:{}", static_cast<int>(result),
        name, description, location.file_name(), location.line()
    );
    if constexpr (ShouldThrow)
        throw kira::Anyhow("{}", message);
    else
        LogError("{}", message);
}
} // namespace flux
