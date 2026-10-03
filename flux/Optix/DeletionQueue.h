#pragma once

#include <cuda_runtime_api.h>

#include <deque>
#include <memory>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
/// \brief Deletes objects once the stream finishes the work before them.
class DeletionQueue final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Creates an empty queue for a stream.
    explicit DeletionQueue(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}
    ~DeletionQueue();

    using CudaStreamMixin::getStream;

    /// \brief Deletes an object once the stream finishes its current work.
    ///
    /// A later call to deleteFinished() deletes it. If host memory runs out, this call waits for
    /// the stream and deletes the object at once.
    /// \pre Work added to the stream later does not use \p object. Deleting \p object does not
    ///      use this queue.
    void deleteLater(std::shared_ptr<void const> object) noexcept;

    /// \brief Deletes the objects that the stream no longer uses. Does not wait.
    /// \throw kira::Anyhow If CUDA fails.
    void deleteFinished();

private:
    /// Objects passed to deleteLater() before one call to deleteFinished(), and an event that
    /// completes once the stream finishes the work before them.
    struct Batch {
        cudaEvent_t event{};
        std::vector<std::shared_ptr<void const>> objects;
    };

    /// Objects passed to deleteLater() since the last call to deleteFinished().
    std::vector<std::shared_ptr<void const>> objectsWithoutEvent_;
    /// Oldest first. Each event completes after the events before it.
    std::deque<Batch> batches_;
};
} // namespace flux
