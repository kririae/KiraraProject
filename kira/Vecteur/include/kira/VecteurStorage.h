#pragma once

#include <span>

#include "kira/Compiler.h"

namespace kira {
template <typename Scalar, std::size_t Size, std::size_t alignment> struct VecteurStorage {
    alignas(alignment) Scalar storage[Size];

    /// Check if the storage is dynamic, i.e., on heap.
    [[nodiscard]] static constexpr bool is_dynamic() { return false; }

public:
    [[nodiscard]] constexpr size_t size() const { return Size; }

    [[nodiscard]] constexpr Scalar *data() KIRA_LIFETIME_BOUND { return storage; }
    [[nodiscard]] constexpr Scalar const *data() const KIRA_LIFETIME_BOUND { return storage; }

    [[nodiscard]] constexpr auto begin() KIRA_LIFETIME_BOUND { return data(); }
    [[nodiscard]] constexpr auto begin() const KIRA_LIFETIME_BOUND { return data(); }

    [[nodiscard]] constexpr auto end() KIRA_LIFETIME_BOUND { return data() + size(); }
    [[nodiscard]] constexpr auto end() const KIRA_LIFETIME_BOUND { return data() + size(); }

public:
    [[nodiscard]] constexpr auto to_span() && = delete;
    [[nodiscard]] constexpr auto to_span() & { return std::span{data(), size()}; }
    [[nodiscard]] constexpr auto to_span() const & { return std::span{data(), size()}; }

    [[nodiscard]] constexpr auto to_array() {
        std::array<Scalar, Size> arr;
        std::copy_n(begin(), size(), arr.begin());
        return arr;
    }
};

template <typename Scalar, std::size_t alignment>
struct VecteurStorage<Scalar, std::dynamic_extent, alignment> {
private:
    Scalar *storage{nullptr};
    std::size_t asize{0};

public:
    /// Check if the storage is dynamic, i.e., on heap.
    [[nodiscard]] static constexpr bool is_dynamic() { return true; }

public:
    VecteurStorage() = default;

#if 0
    constexpr VecteurStorage(Scalar *storage, std::size_t size) : storage{storage}, asize{size} {
        // This cannot be checked at compile-time.
        KIRA_ASSERT(storage % alignment == 0, "The storage must be aligned to {}.", alignment);
    }
#endif

    constexpr ~VecteurStorage() { delete[] storage; }

    constexpr VecteurStorage(std::size_t size)
        : storage(new(std::align_val_t{alignment}) Scalar[size]), asize{size} {}

    constexpr VecteurStorage(VecteurStorage const &rhs)
        : storage(new(std::align_val_t{alignment}) Scalar[rhs.asize]), asize{rhs.asize} {
        std::copy_n(rhs.storage, asize, storage);
    }

    constexpr VecteurStorage(VecteurStorage &&rhs) noexcept
        : storage{rhs.storage}, asize{rhs.asize} {
        rhs.storage = nullptr;
        rhs.asize = 0;
    }

    friend void swap(VecteurStorage &lhs, VecteurStorage &rhs) {
        using std::swap;
        swap(lhs.storage, rhs.storage);
        swap(lhs.asize, rhs.asize);
    }

    constexpr VecteurStorage &operator=(VecteurStorage rhs) {
        using std::swap;
        swap(*this, rhs);
        return *this;
    }

public:
    [[nodiscard]] constexpr size_t size() const { return asize; }

    [[nodiscard]] constexpr Scalar *data() KIRA_LIFETIME_BOUND { return storage; }
    [[nodiscard]] constexpr Scalar const *data() const KIRA_LIFETIME_BOUND { return storage; }

    [[nodiscard]] constexpr auto begin() KIRA_LIFETIME_BOUND { return data(); }
    [[nodiscard]] constexpr auto begin() const KIRA_LIFETIME_BOUND { return data(); }

    [[nodiscard]] constexpr auto end() KIRA_LIFETIME_BOUND { return data() + size(); }
    [[nodiscard]] constexpr auto end() const KIRA_LIFETIME_BOUND { return data() + size(); }

    [[nodiscard]] constexpr auto to_span() && = delete;
    [[nodiscard]] constexpr auto to_span() & { return std::span{data(), size()}; }
    [[nodiscard]] constexpr auto to_span() const & { return std::span{data(), size()}; }

    [[nodiscard]] constexpr auto to_array() = delete;

    [[nodiscard]] constexpr auto realloc(std::size_t size) {
        auto newStorage = VecteurStorage{size};
        using std::swap;
        swap(*this, newStorage);
    }
};
} // namespace kira
