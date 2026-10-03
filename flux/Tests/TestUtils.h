#pragma once

#include <cuda_runtime_api.h>

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

#include "flux/Core/HostBuffer.h"

namespace flux::test {
/// \brief Returns a shared buffer of the arguments, with one spare element.
template <typename T, std::convertible_to<T>... Rest>
[[nodiscard]] Shared<HostBuffer<T>> sharedBuffer(T first, Rest... rest) {
    auto buffer = std::make_shared<HostBuffer<T>>();
    auto constexpr count = 1 + sizeof...(Rest);
    buffer->resize(count, count + 1);

    // Write the arguments in order.
    auto *out = buffer->data();
    *out = std::move(first);
    ((*++out = static_cast<T>(rest)), ...);
    return buffer;
}

/// \brief Returns whether the CUDA Runtime API can access a device.
[[nodiscard]] inline bool hasCudaDevice() {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

/// \brief Returns whether the current CUDA device supports stream-ordered allocation.
[[nodiscard]] inline bool hasCudaMemoryPoolSupport() {
    if (!hasCudaDevice())
        return false;

    int device = 0;
    int supported = 0;
    return cudaGetDevice(&device) == cudaSuccess &&
           cudaDeviceGetAttribute(&supported, cudaDevAttrMemoryPoolsSupported, device) ==
               cudaSuccess &&
           supported != 0;
}

/// \brief Blocks a stream until open() is called, the gate is destroyed, or ten seconds pass.
class StreamGate {
public:
    explicit StreamGate(cudaStream_t stream)
        : stream_(stream), watchdog_([this](std::stop_token const &stop) {
              auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
              while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
              open();
          }) {
        if (cudaLaunchHostFunc(stream, &wait, this) != cudaSuccess)
            throw std::runtime_error("StreamGate: cudaLaunchHostFunc failed");
    }

    StreamGate(StreamGate const &) = delete;
    StreamGate &operator=(StreamGate const &) = delete;

    /// \brief Opens the gate, and waits for the stream.
    ~StreamGate() {
        open();
        // After a CUDA error, the stream may skip the gate, so wait for the stream itself.
        cudaStreamSynchronize(stream_);
    }

    /// \brief Lets the stream pass the gate.
    void open() noexcept { isOpen_.store(true); }

private:
    static void CUDART_CB wait(void *gate) {
        auto const &self = *static_cast<StreamGate const *>(gate);
        while (!self.isOpen_.load())
            std::this_thread::yield();
    }

    cudaStream_t stream_;
    std::atomic<bool> isOpen_{false};
    std::jthread watchdog_;
};
} // namespace flux::test
