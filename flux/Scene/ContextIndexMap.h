#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include "flux/Core/Object.h"
#include "kira/SmallVector.h"

namespace flux {
class Context;

/// \brief Maps Context object IDs to reusable array indices.
///
/// An index remains unchanged while its context ID is present. Erased indices
/// may be assigned to IDs from a later transaction.
class ContextIndexMap final : private Noncopyable {
    friend class Context;

public:
    using Index = std::uint32_t;

    /// \brief Collects IDs published together by one Context transaction.
    class Transaction final : private Noncopyable {
        friend class ContextIndexMap;

    public:
        void insert(std::size_t contextId);

    private:
        std::map<std::size_t, Index> entries_;
    };

    /// \brief Publishes the IDs in \p tx and assigns each one an index.
    void merge(Transaction &&tx);

    /// \brief Removes \p contextId and returns its released index.
    ///
    /// \throw std::out_of_range If \p contextId is not present.
    [[nodiscard]] Index erase(std::size_t contextId);

    /// \brief Returns the index assigned to \p contextId.
    ///
    /// \throw std::out_of_range If \p contextId is not present.
    [[nodiscard]] Index getIndex(std::size_t contextId) const;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// \brief Returns one past the largest index ever assigned.
    [[nodiscard]] Index getIndexLimit() const noexcept { return indexLimit_; }

private:
    void validateMerge(Transaction const &tx) const;
    void mergeValidated(Transaction &&tx) noexcept;

    std::map<std::size_t, Index> entries_;
    kira::SmallVector<Index> freeIndices_;
    Index indexLimit_{};
};
} // namespace flux
