#pragma once

#include <span>

#include "kira/Compiler.h"
#include "kira/Types.h"

namespace kira {
template <typename Scalar_, std::size_t Size_, typename Derived> struct VecteurBase;

template <typename Scalar_, typename Derived>
struct VecteurBase<Scalar_, std::dynamic_extent, Derived> {};

template <typename Scalar_, std::size_t Size_, typename Derived> struct VecteurBase {
public:
    using Scalar = Scalar_;
    static constexpr std::size_t Size = Size_;

    /// \name CRTP interface
    /// \{

    /// Get the derived class.
    template <bool is_constant_evaluated = false>
    [[nodiscard]] constexpr auto const &derived() const KIRA_LIFETIME_BOUND {
        if constexpr (is_constant_evaluated)
            return static_cast<typename Derived::ConstexprImpl const &>(*this);
        else
            return static_cast<Derived const &>(*this);
    }

    /// \copydoc derived
    template <bool is_constant_evaluated = false>
    [[nodiscard]] constexpr auto &derived() KIRA_LIFETIME_BOUND {
        if constexpr (is_constant_evaluated)
            return static_cast<typename Derived::ConstexprImpl &>(*this);
        else
            return static_cast<Derived &>(*this);
    }

    /// \}
public:
    /// \name Element access interface
    /// \{

    [[nodiscard]] constexpr decltype(auto) operator[](auto i) const { return derived().entry(i); }

    /// \copydoc operator[]
    [[nodiscard]] constexpr decltype(auto) operator[](auto i) { return derived().entry(i); }

    /// Get the first element of the vector.
    [[nodiscard]] constexpr decltype(auto) x() const
        requires(Size >= 1)
    {
        return derived().entry(0_U);
    }

    /// Get the second element of the vector.
    [[nodiscard]] constexpr decltype(auto) y() const
        requires(Size >= 2)
    {
        return derived().entry(1_U);
    }

    /// Get the third element of the vector.
    [[nodiscard]] constexpr decltype(auto) z() const
        requires(Size >= 3)
    {
        return derived().entry(2_U);
    }

    /// Get the forth element of the vector.
    [[nodiscard]] constexpr decltype(auto) w() const
        requires(Size >= 4)
    {
        return derived().entry(3_U);
    }

    /// Get the first element of the vector.
    [[nodiscard]] constexpr decltype(auto) x()
        requires(Size >= 1)
    {
        return derived().entry(0_U);
    }

    /// Get the second element of the vector.
    [[nodiscard]] constexpr decltype(auto) y()
        requires(Size >= 2)
    {
        return derived().entry(1_U);
    }

    /// Get the third element of the vector.
    [[nodiscard]] constexpr decltype(auto) z()
        requires(Size >= 3)
    {
        return derived().entry(2_U);
    }

    /// Get the forth element of the vector.
    [[nodiscard]] constexpr decltype(auto) w()
        requires(Size >= 4)
    {
        return derived().entry(3_U);
    }

    /// \}
public:
};

/// \name Forward declarations
/// \{
/// Forward declaration of the implementation of the vector.
///
/// \tparam Scalar The scalar type of the vector.
/// \tparam Size The size of the vector.
/// \tparam IsConstexpr Whether the implementation supports constexpr.
/// \tparam Derived The CRTP derived class.
template <typename Scalar, std::size_t Size, bool IsConstexpr, typename Derived> struct VecteurImpl;

template <typename Scalar, std::size_t Size> struct Vecteur;
/// \}
} // namespace kira
