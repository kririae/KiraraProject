#pragma once

#include <cstddef>
#include <span>

#include "kira/Compiler.h"

namespace kira {
template <typename Scalar, std::size_t Size, std::size_t alignment> struct VecteurStorage {
    alignas(alignment) Scalar storage[Size];

    /// Check if the storage is dynamic.
    [[nodiscard]] static constexpr bool is_dynamic() { return false; }

public:
    [[nodiscard]] constexpr size_t size() const { return Size; }

    [[nodiscard]] constexpr Scalar *data() KIRA_LIFETIME_BOUND { return storage; }
    [[nodiscard]] constexpr Scalar const *data() const KIRA_LIFETIME_BOUND { return storage; }

    [[nodiscard]] constexpr auto begin() KIRA_LIFETIME_BOUND { return data(); }
    [[nodiscard]] constexpr auto begin() const KIRA_LIFETIME_BOUND { return data(); }

    [[nodiscard]] constexpr auto end() KIRA_LIFETIME_BOUND { return data() + size(); }
    [[nodiscard]] constexpr auto end() const KIRA_LIFETIME_BOUND { return data() + size(); }
};

template <typename Scalar, std::size_t alignment>
struct VecteurStorage<Scalar, std::dynamic_extent, alignment> {
    Scalar *storage{nullptr};
    std::size_t size{0};

    /// Check if the storage is dynamic.
    [[nodiscard]] static constexpr bool is_dynamic() { return true; }

public:
    // TODO(krr): implement a copyable heap storage class.
};
} // namespace kira
