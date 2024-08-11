#pragma once

#include <span>

#include "kira/Compiler.h"
#include "kira/VecteurTraits.h"

namespace kira {
//! NOTE(krr): Copy assignment matrix:
//! | this \ rhs | static | dynamic | rvalue dynamic |
//! |------------|--------|---------|----------------|
//! | static     | Y(1)   | N(2)    | N(5)           |
//! | dynamic    | Y(3)   | Y(4)    | Y(6)           |
//! |------------|--------|---------|----------------|
//!
//! (1) statically checked
//! (2) not safe
//! (3) safe and dynamically checked, realloc if different size
//! (4) safe and dynamically checked, realloc if different size
//! (5) not safe
//! (6) safe and dynamically checked, no realloc
//!
//! Storage implements all the constructors and assignment operators for the vector classes, s.t.
//! code can be reused for different leaf node types.

template <typename Scalar, std::size_t Size, std::size_t alignment> struct VecteurStorage {
    alignas(alignment) Scalar storage[Size];

    /// Check if the storage is dynamic, i.e., on heap.
    [[nodiscard]] static constexpr bool is_dynamic() { return false; }

public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Static Constructors
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Default constructor.
    VecteurStorage() = default;

    /// Construct a vector with all elements set to `v`.
    explicit constexpr VecteurStorage(Scalar const &v) {
        // Use a range-based for loop to initialize the elements.
        for (auto &x : *this)
            x = v;
    }

    /// Construct a vector with the elements set to `ts...`.
    template <typename... Ts>
    constexpr VecteurStorage(Ts... ts)
        requires(sizeof...(Ts) == Size)
        : storage{static_cast<Scalar>(ts)...} {
        static_assert(
            std::conjunction_v<is_safely_convertible_t<Ts, Scalar>...>,
            "The types of the arguments must be safely convertible to the scalar type."
        );
    }

    /// Construct a vector with the elements from a span.
    template <typename RHS> explicit constexpr VecteurStorage(std::span<RHS, Size> sp) {
        static_assert(
            is_safely_convertible<RHS, Scalar>,
            "The types must be safely convertible to the scalar."
        );

        std::copy_n(sp.begin(), this->size(), this->begin());
    }

    /// Construct a vector with the elements from an array.
    template <typename RHS> explicit constexpr VecteurStorage(std::array<RHS, Size> const &arr) {
        static_assert(
            is_safely_convertible<RHS, Scalar>,
            "The types must be safely convertible to the scalar."
        );

        std::copy_n(arr.begin(), this->size(), this->begin());
    }

    /// Construct a vector with the elements from another vector.
    ///
    /// \note The `explicit` here bans construction from other backends.
    explicit constexpr VecteurStorage(VecteurStorage const &rhs) {
        std::copy_n(rhs.begin(), this->size(), this->begin());
    }

    /// Construct a vector with elements from another different-typed vector.
    ///
    /// \note The `explicit` here bans construction from other backends.
    template <is_vecteur RHS>
    explicit constexpr VecteurStorage(RHS const &rhs)
        requires(Size == RHS::Size)
    {
        static_assert(
            is_safely_convertible<RHS, Scalar>,
            "The types must be safely convertible to the scalar."
        );

        std::copy_n(rhs.begin(), this->size(), this->begin());
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Copy-assignment operators
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

#if 0
    // (1) static from static
    constexpr auto operator=(VecteurStorage const &rhs) = default;
#endif

    // (1') static from static, different type
    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires((not RHS::is_dynamic) and is_safely_convertible<typename RHS::Scalar, Scalar>)
    {
        static_assert(
            is_static_operable<Size, RHS::Size>,
            "The scalar types of the operands must be the same."
        );
        std::copy_n(rhs.begin(), this->size(), this->begin());
        return *this;
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------

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

public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Dynamic Constructors
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Construct a vector with the elements from an initializer list.
    explicit VecteurStorage(std::initializer_list<Scalar> init) : VecteurStorage(init.size()) {
        std::copy_n(init.begin(), this->size(), this->begin());
    }

    /// Construct a vector with all elements set to `v`.
    explicit constexpr VecteurStorage(std::size_t size, Scalar const &v) : VecteurStorage(size) {
        std::fill_n(this->begin(), size, v);
    }

    /// Construct a vector with the elements from a span.
    template <typename RHS, size_t ASize>
    explicit constexpr VecteurStorage(std::span<RHS, ASize> sp)
        requires(is_safely_convertible<RHS, Scalar>)
        : VecteurStorage(sp.size()) {
        std::copy_n(sp.begin(), this->size(), this->begin());
    }

    /// Construct a vector with the elements from an array.
    template <typename RHS, size_t ASize>
    explicit constexpr VecteurStorage(std::array<RHS, ASize> const &arr)
        requires(is_safely_convertible<RHS, Scalar>)
        : VecteurStorage(arr.size()) {
        std::copy_n(arr.begin(), this->size(), this->begin());
    }

    /// Construct a vector with elements from another different-typed vector.
    template <is_vecteur RHS>
    explicit constexpr VecteurStorage(RHS const &rhs)
        requires(is_safely_convertible<typename RHS::Scalar, Scalar>)
        : VecteurStorage(rhs.size()) {
        std::copy_n(rhs.begin(), this->size(), this->begin());
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------

public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Copy-assignment operators
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    // (6) dynamic from rvalue dynamic
    constexpr decltype(auto) operator=(VecteurStorage &&rhs) noexcept {
        VecteurStorage tmp{std::move(rhs)};
        using std::swap;
        swap(*this, tmp);
        return *this;
    }

    // (4) dynamic from dynamic
    constexpr decltype(auto) operator=(VecteurStorage const &rhs) {
        if (this == &rhs)
            return *this;
        if (this->size() != rhs.size())
            this->realloc(rhs.size());
        std::copy_n(rhs.begin(), this->size(), this->begin());
        return *this;
    }

    // (4') dyanmic from dynamic, different type
    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires(RHS::is_dynamic() and is_safely_convertible<typename RHS::Scalar, Scalar>)
    {
        if (this->size() != rhs.size())
            this->realloc(rhs.size());
        std::copy_n(rhs.begin(), this->size(), this->begin());
        return *this;
    }

    // (3) dynamic from static, all types
    template <is_vecteur RHS>
    constexpr decltype(auto) operator=(RHS const &rhs)
        requires((not RHS::is_dynamic()) and is_safely_convertible<typename RHS::Scalar, Scalar>)
    {
        static_assert(not RHS::is_dynamic(), "The RHS must be a static vector.");
        if (this->size() != rhs.size())
            this->realloc(rhs.size());
        std::copy_n(rhs.begin(), this->size(), this->begin());
        return *this;
    }

    /// \{
    // -----------------------------------------------------------------------------------------------------------------

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
        auto newStorage = VecteurStorage(size);
        using std::swap;
        swap(*this, newStorage);
    }
};
} // namespace kira
