#include "flux/Optix/DeletionQueue.h"

#include <utility>

namespace flux {
DeletionQueue::~DeletionQueue() {
    cudaCheck<false>(cudaStreamSynchronize(getStream()));

    for (auto const &batch : batches_)
        cudaCheck<false>(cudaEventDestroy(batch.event));
}

void DeletionQueue::deleteLater(std::shared_ptr<void const> object) noexcept {
    try {
        objectsWithoutEvent_.push_back(std::move(object));
    } catch (...) {
        // The queue cannot keep the object, so wait until the stream no longer uses it.
        cudaCheck<false>(cudaStreamSynchronize(getStream()));
    }
}

void DeletionQueue::deleteFinished() {
    // One event covers every object passed to deleteLater() since the last call, because it
    // completes after all work before it.
    if (!objectsWithoutEvent_.empty()) {
        auto &batch = batches_.emplace_back();

        auto error = cudaEventCreateWithFlags(&batch.event, cudaEventDisableTiming);
        if (error == cudaSuccess) {
            error = cudaEventRecord(batch.event, getStream());
            if (error != cudaSuccess)
                cudaCheck<false>(cudaEventDestroy(batch.event));
        }

        if (error != cudaSuccess) {
            batches_.pop_back();
            cudaCheck(error);
        }

        batch.objects = std::move(objectsWithoutEvent_);
        objectsWithoutEvent_.clear();
    }

    // Events complete in stream order: if one has not completed, the later ones have not
    // either.
    while (!batches_.empty()) {
        auto const queryError = cudaEventQuery(batches_.front().event);
        if (queryError == cudaErrorNotReady)
            return;
        cudaCheck(queryError);

        auto const destroyError = cudaEventDestroy(batches_.front().event);
        batches_.pop_front();
        cudaCheck(destroyError);
    }
}
} // namespace flux
