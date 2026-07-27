#pragma once

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cuda/std/span>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixUtils.h"
#include "kira/Assertions.h"

namespace flux {
/// \brief Owns a contiguous array in CUDA device memory.
///
/// Allocation, known uses, and release are ordered on the lifetime stream.
/// Operations that access the existing allocation publish their explicit stream
/// before error checking because CUDA may report an error from earlier
/// asynchronous work. Replacement operations keep the old binding until the new
/// state is ready. Callers must establish ordering before using or rebinding the
/// allocation on another stream. The buffer does not own its stream.
///
/// \tparam T Trivially copyable element type.
template <typename T> class DeviceBuffer final : private Noncopyable, public CudaStreamMixin {
    static_assert(!std::is_const_v<T>, "DeviceBuffer cannot store const elements");
    static_assert(
        std::is_trivially_copyable_v<T>, "DeviceBuffer requires trivially copyable elements"
    );

public:
    using value_type = T;

    /// \brief Constructs an empty buffer bound to the legacy default stream.
    DeviceBuffer() noexcept = default;

    /// \brief Constructs an empty buffer bound to \p stream.
    /// \param stream Stream that will order the buffer's lifetime.
    explicit DeviceBuffer(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Takes ownership of \p other without synchronizing its stream.
    DeviceBuffer(DeviceBuffer &&other) noexcept { swap(*this, other); }

    /// \brief Replaces this allocation with the one owned by \p other.
    ///
    /// The previous allocation is released on this buffer's lifetime stream.
    DeviceBuffer &operator=(DeviceBuffer &&other) noexcept {
        DeviceBuffer replacement(std::move(other));
        swap(*this, replacement);
        return *this;
    }

    /// \brief Releases the allocation on its lifetime stream.
    ///
    /// The lifetime stream must still be valid. A deallocation failure is
    /// fatal because ownership can no longer be represented safely.
    ~DeviceBuffer() noexcept { clear(); }

    /// \brief Replaces the allocation without preserving its contents.
    ///
    /// A same-size resize is a no-op and does not change the lifetime stream.
    /// \param count Number of elements in the replacement allocation.
    /// \throw std::invalid_argument If the byte size overflows.
    /// \throw kira::Anyhow If CUDA cannot enqueue the allocation.
    void resize(std::size_t count) { resize(count, getStream()); }

    /// \brief Replaces the allocation on \p stream without preserving contents.
    ///
    /// A same-size resize is a no-op and does not publish \p stream.
    /// \param count Number of elements in the replacement allocation.
    /// \param stream Stream used to allocate and release storage.
    /// \throw std::invalid_argument If the byte size overflows.
    /// \throw kira::Anyhow If CUDA cannot enqueue the allocation.
    void resize(std::size_t count, cudaStream_t stream) {
        KIRA_ASSERT(invariantHolds(), "DeviceBuffer invariant violated before resize");
        if (count == size_)
            return;
        if (count == 0) {
            clear(stream);
            return;
        }
        validateCount(count);

        auto *replacement = allocate(count, stream);
        deallocate(data_, stream);
        data_ = replacement;
        size_ = count;
        setStream(stream);
        KIRA_ASSERT(invariantHolds(), "DeviceBuffer invariant violated after resize");
    }

    /// \brief Releases the allocation on the currently bound stream.
    void clear() noexcept { clear(getStream()); }

    /// \brief Releases the allocation on \p stream and binds the empty buffer to it.
    /// \param stream Stream ordered after all uses of the allocation.
    void clear(cudaStream_t stream) noexcept {
        KIRA_ASSERT(invariantHolds(), "DeviceBuffer invariant violated before clear");
        deallocate(data_, stream);
        data_ = nullptr;
        size_ = 0;
        setStream(stream);
    }

    /// \brief Zeroes the allocation on the currently bound stream.
    /// \throw kira::Anyhow If CUDA cannot enqueue the operation.
    void zero() { zero(getStream()); }

    /// \brief Zeroes the allocation on \p stream.
    /// \param stream Stream ordered after the allocation and its prior uses.
    /// \throw kira::Anyhow If CUDA cannot enqueue the operation.
    void zero(cudaStream_t stream) {
        if (empty())
            return;

        setStream(stream);
        cudaCheck(cudaMemsetAsync(data_, 0, size_ * sizeof(T), stream));
    }

    /// \brief Replaces the contents from host storage on the bound stream.
    ///
    /// The host storage must remain alive until the copy completes.
    /// \param source Elements to copy.
    /// \throw std::invalid_argument If the byte size overflows.
    /// \throw kira::Anyhow If CUDA cannot enqueue the allocation or copy.
    void copyFromHost(cuda::std::span<T const> source) { copyFromHost(source, getStream()); }

    /// \brief Replaces the contents from host storage on \p stream.
    ///
    /// The host storage must remain alive until the copy completes.
    /// \param source Elements to copy.
    /// \param stream Stream ordered after the allocation and its prior uses.
    /// \throw std::invalid_argument If the byte size overflows.
    /// \throw kira::Anyhow If CUDA cannot enqueue the allocation or copy.
    void copyFromHost(cuda::std::span<T const> source, cudaStream_t stream) {
        if (source.size() == size_) {
            if (source.empty())
                return;
            setStream(stream);
            cudaCheck(cudaMemcpyAsync(
                data_, source.data(), source.size_bytes(), cudaMemcpyHostToDevice, stream
            ));
            return;
        }
        if (source.empty()) {
            clear(stream);
            return;
        }
        validateCount(source.size());

        auto *replacement = allocate(source.size(), stream);
        try {
            cudaCheck(cudaMemcpyAsync(
                replacement, source.data(), source.size_bytes(), cudaMemcpyHostToDevice, stream
            ));
        } catch (...) {
            deallocate(replacement, stream);
            throw;
        }

        deallocate(data_, stream);
        data_ = replacement;
        size_ = source.size();
        setStream(stream);
    }

    /// \brief Copies the contents to same-sized host storage on the bound stream.
    ///
    /// The host storage must remain alive until the copy completes.
    /// \param destination Host storage that receives the elements.
    /// \throw std::invalid_argument If \p destination has a different size.
    /// \throw kira::Anyhow If CUDA cannot enqueue the copy.
    void copyToHost(cuda::std::span<T> destination) { copyToHost(destination, getStream()); }

    /// \brief Copies the contents to same-sized host storage on \p stream.
    ///
    /// The host storage must remain alive until the copy completes.
    /// \param destination Host storage that receives the elements.
    /// \param stream Stream ordered after the allocation and its prior uses.
    /// \throw std::invalid_argument If \p destination has a different size.
    /// \throw kira::Anyhow If CUDA cannot enqueue the copy.
    void copyToHost(cuda::std::span<T> destination, cudaStream_t stream) {
        if (destination.size() != size_)
            throw std::invalid_argument("DeviceBuffer: destination size does not match");
        if (destination.empty())
            return;

        setStream(stream);
        cudaCheck(cudaMemcpyAsync(
            destination.data(), data_, destination.size_bytes(), cudaMemcpyDeviceToHost, stream
        ));
    }

    /// \brief Returns the device pointer, or null when empty.
    [[nodiscard]] T *data() noexcept { return data_; }

    /// \brief Returns the device pointer, or null when empty.
    [[nodiscard]] T const *data() const noexcept { return data_; }

    /// \brief Returns the number of stored elements.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// \brief Returns whether the buffer is empty.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// \brief Returns a mutable device-memory view.
    ///
    /// The returned span must not be dereferenced by host code.
    [[nodiscard]] cuda::std::span<T> span() noexcept { return {data_, size_}; }

    /// \brief Returns a read-only device-memory view.
    ///
    /// The returned span must not be dereferenced by host code.
    [[nodiscard]] cuda::std::span<T const> span() const noexcept { return {data_, size_}; }

    friend void swap(DeviceBuffer &lhs, DeviceBuffer &rhs) noexcept {
        using std::swap;
        swap(static_cast<CudaStreamMixin &>(lhs), static_cast<CudaStreamMixin &>(rhs));
        swap(lhs.data_, rhs.data_);
        swap(lhs.size_, rhs.size_);
    }

private:
    [[nodiscard]] static T *allocate(std::size_t count, cudaStream_t stream) {
        void *storage = nullptr;
        cudaCheck(cudaMallocAsync(&storage, count * sizeof(T), stream));
        KIRA_FORCE_ASSERT(storage != nullptr, "cudaMallocAsync returned null storage");
        return static_cast<T *>(storage);
    }

    static void deallocate(T *storage, cudaStream_t stream) noexcept {
        if (!storage)
            return;

        auto const result = cudaFreeAsync(storage, stream);
        if (result != cudaSuccess) {
            cudaCheck<false>(result);
            std::terminate();
        }
    }

    static void validateCount(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::invalid_argument("DeviceBuffer: element count exceeds addressable bytes");
    }

    [[nodiscard]] bool invariantHolds() const noexcept {
        return (data_ == nullptr) == (size_ == 0);
    }

    T *data_{};
    std::size_t size_{};
};
} // namespace flux
