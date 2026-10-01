#include "flux/Scene/ContextIndexMap.h"

#include <limits>
#include <stdexcept>

#include "kira/Anyhow.h"
#include "kira/Assertions.h"

namespace flux {
namespace {
constexpr auto invalidIndex = std::numeric_limits<ContextIndexMap::Index>::max();
}

void ContextIndexMap::Transaction::insert(std::size_t contextId) {
    auto const [unused, inserted] = entries_.emplace(contextId, invalidIndex);
    if (!inserted)
        throw kira::Anyhow("ContextIndexMap: context ID is already registered");
}

void ContextIndexMap::merge(Transaction &&tx) {
    validateMerge(tx);
    mergeValidated(std::move(tx));
}

ContextIndexMap::Index ContextIndexMap::erase(std::size_t contextId) {
    auto const iterator = entries_.find(contextId);
    if (iterator == entries_.end())
        throw std::out_of_range("ContextIndexMap: context ID is not registered");

    // Reserve first, so that neither this call nor \c recycle can fail after the erase.
    releasedIndices_.reserve(releasedIndices_.size() + 1);
    freeIndices_.reserve(freeIndices_.size() + releasedIndices_.size() + 1);

    auto const index = iterator->second;
    releasedIndices_.push_back(index);
    entries_.erase(iterator);
    return index;
}

void ContextIndexMap::recycle() noexcept {
    for (auto const index : releasedIndices_)
        freeIndices_.push_back(index);
    releasedIndices_.clear();
}

ContextIndexMap::Index ContextIndexMap::getIndex(std::size_t contextId) const {
    auto const iterator = entries_.find(contextId);
    if (iterator == entries_.end())
        throw std::out_of_range("ContextIndexMap: context ID is not registered");
    return iterator->second;
}

void ContextIndexMap::validateMerge(Transaction const &tx) const {
    for (auto const &[contextId, unused] : tx.entries_)
        if (entries_.contains(contextId))
            throw kira::Anyhow("ContextIndexMap: context ID is already registered");

    auto const newIndices =
        tx.entries_.size() > freeIndices_.size() ? tx.entries_.size() - freeIndices_.size() : 0;
    auto const availableIndices = static_cast<std::size_t>(invalidIndex - indexLimit_);
    if (newIndices > availableIndices)
        throw kira::Anyhow("ContextIndexMap: index range is exhausted");
}

void ContextIndexMap::mergeValidated(Transaction &&tx) noexcept {
    while (!tx.entries_.empty()) {
        auto node = tx.entries_.extract(tx.entries_.begin());
        if (freeIndices_.empty()) {
            node.mapped() = indexLimit_;
            ++indexLimit_;
        } else {
            node.mapped() = freeIndices_.back();
            freeIndices_.pop_back();
        }

        auto const result = entries_.insert(std::move(node));
        KIRA_ASSERT(result.inserted, "ContextIndexMap merge must publish every transaction entry");
    }
}
} // namespace flux
