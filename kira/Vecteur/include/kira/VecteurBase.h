#pragma once

#include <span>

#include "kira/Macros.h"
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

    /// Get the derived class based on consteval context.
    template <bool is_constant_evaluated>
    [[nodiscard]] constexpr auto &derivedDispatcher() KIRA_LIFETIME_BOUND {
        if constexpr (is_constant_evaluated)
            return static_cast<Derived &>(*this);
        else
            return static_cast<Derived &>(*this);
    }

    /// \copydoc derivedDispatcher
    template <bool is_constant_evaluated>
    [[nodiscard]] constexpr auto const &derivedDispatcher() const KIRA_LIFETIME_BOUND {
        if constexpr (is_constant_evaluated)
            return static_cast<Derived const &>(*this);
        else
            return static_cast<Derived const &>(*this);
    }

    /// Get the derived class.
    [[nodiscard]] constexpr auto const &derived() const KIRA_LIFETIME_BOUND {
        return derivedDispatcher<false>();
    }

    /// \copydoc derived
    [[nodiscard]] constexpr auto &derived() KIRA_LIFETIME_BOUND {
        return derivedDispatcher<false>();
    }

    /// \}
public:
    /// \name Element access interface
    /// \{

    [[nodiscard]] constexpr decltype(auto) operator[](auto i) const { return derived().entry(i); }

    /// \copydoc operator[]
    [[nodiscard]] constexpr decltype(auto) operator[](auto i) { return derived().entry(i); }

    [[nodiscard]] constexpr decltype(auto) x() const
        requires(Size >= 1)
    {
        return derived().entry(0_u);
    }

    [[nodiscard]] constexpr decltype(auto) y() const
        requires(Size >= 2)
    {
        return derived().entry(1_u);
    }

    [[nodiscard]] constexpr decltype(auto) z() const
        requires(Size >= 3)
    {
        return derived().entry(2_u);
    }

    [[nodiscard]] constexpr decltype(auto) w() const
        requires(Size >= 4)
    {
        return derived().entry(3_u);
    }

    [[nodiscard]] constexpr decltype(auto) x()
        requires(Size >= 1)
    {
        return derived().entry(0_u);
    }

    [[nodiscard]] constexpr decltype(auto) y()
        requires(Size >= 2)
    {
        return derived().entry(1_u);
    }

    [[nodiscard]] constexpr decltype(auto) z()
        requires(Size >= 3)
    {
        return derived().entry(2_u);
    }

    [[nodiscard]] constexpr decltype(auto) w()
        requires(Size >= 4)
    {
        return derived().entry(3_u);
    }

    /// \}
public:
};

/// \name Forward declarations
/// \{
template <typename Scalar, std::size_t Size, typename Derived> struct VecteurImpl;

template <typename Scalar, std::size_t Size> struct Vecteur;
/// \}
} // namespace kira
