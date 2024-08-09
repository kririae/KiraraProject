#pragma once

#include <type_traits>

#include "kira/Compiler.h"
#include "kira/Types.h"

namespace kira {
template <typename Scalar_, std::size_t Size_, typename Derived> struct VecteurBase;

//! The base class is not specialized for dynamic extent, because std::dynamic_extent holds for the
//! (Size > std::dynamic_extent) cases.
#if 0
template <typename Scalar_, typename Derived>
struct VecteurBase<Scalar_, std::dynamic_extent, Derived> {};
#endif

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
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Element access interface
    // -----------------------------------------------------------------------------------------------------------------
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
    // -----------------------------------------------------------------------------------------------------------------
public:
#define KIRA_CONSTEXPR_DISPATCH0(func)                                                             \
    [&]() {                                                                                        \
        return std::is_constant_evaluated() ? derived<true>().func() : derived<false>().func();    \
    }()
#define KIRA_CONSTEXPR_DISPATCH1(func, p1)                                                         \
    [&]() {                                                                                        \
        return std::is_constant_evaluated() ? derived<true>().func(p1)                             \
                                            : derived<false>().func(p1);                           \
    }()
#define KIRA_CONSTEXPR_DISPATCH2(func, p1, p2)                                                     \
    [&]() {                                                                                        \
        return std::is_constant_evaluated() ? derived<true>().func(p1, p2)                         \
                                            : derived<false>().func(p1, p2);                       \
    }()
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Binary comparable interface
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Element-wise maximum of two vectors.
    ///
    /// \param rhs can be a scalar or a vector.
    [[nodiscard]] constexpr auto max(auto const &rhs) const {
        return KIRA_CONSTEXPR_DISPATCH1(max_, rhs);
    }

    /// Element-wise minimum of two vectors.
    ///
    /// \param rhs can be a scalar or a vector.
    [[nodiscard]] constexpr auto min(auto const &rhs) const {
        return KIRA_CONSTEXPR_DISPATCH1(min_, rhs);
    }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Reduction arithmetic interface
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Dot product of two vectors.
    ///
    /// \note This function is only available when the size of the vectors is the same.
    /// \note Runtime-check will be enabled in debug mode.
    [[nodiscard]] constexpr auto dot(auto const &rhs) const {
        return KIRA_CONSTEXPR_DISPATCH1(dot_, rhs);
    }

    /// Check if two vectors are equal.
    ///
    /// \note This function is only available when the size of the vectors is the same.
    /// \note Runtime-check will be enabled in debug mode.
    [[nodiscard]] constexpr auto eq(auto const &rhs) const {
        return KIRA_CONSTEXPR_DISPATCH1(eq_, rhs);
    }

    /// Check if two vectors are nearly equal with eps.
    ///
    /// \param epsilon The epsilon value, representing the maximum norm of the difference vector.
    ///
    /// \note This function is only available when the size of the vectors is the same.
    /// \note Runtime-check will be enabled in debug mode.
    [[nodiscard]] constexpr auto near(auto const &rhs, Scalar const &epsilon) const {
        return KIRA_CONSTEXPR_DISPATCH2(near_, rhs, epsilon);
    }

    /// Sum of all elements in the vector.
    [[nodiscard]] constexpr auto hsum() const { return KIRA_CONSTEXPR_DISPATCH0(hsum_); }
    /// Product of all elements in the vector.
    [[nodiscard]] constexpr auto hprod() const { return KIRA_CONSTEXPR_DISPATCH0(hprod_); }
    /// Calculate the maximum element in the vector.
    [[nodiscard]] constexpr auto hmax() const { return KIRA_CONSTEXPR_DISPATCH0(hmax_); }
    /// Calculate the minimum element in the vector.
    [[nodiscard]] constexpr auto hmin() const { return KIRA_CONSTEXPR_DISPATCH0(hmin_); }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------
public:
    // -----------------------------------------------------------------------------------------------------------------
    /// \name Forall arithmetic interface
    // -----------------------------------------------------------------------------------------------------------------
    /// \{

    /// Absolute value of all elements in the vector.
    [[nodiscard]] constexpr auto abs() const { return KIRA_CONSTEXPR_DISPATCH0(abs_); }
    /// Ceiling of all elements in the vector.
    [[nodiscard]] constexpr auto ceil() const { return KIRA_CONSTEXPR_DISPATCH0(ceil_); }
    /// Exponential function applied to all elements in the vector.
    [[nodiscard]] constexpr auto exp() const { return KIRA_CONSTEXPR_DISPATCH0(exp_); }
    /// Floor of all elements in the vector.
    [[nodiscard]] constexpr auto floor() const { return KIRA_CONSTEXPR_DISPATCH0(floor_); }
    /// Natural logarithm of all elements in the vector.
    [[nodiscard]] constexpr auto log() const { return KIRA_CONSTEXPR_DISPATCH0(log_); }
    /// Round all elements in the vector.
    [[nodiscard]] constexpr auto round() const { return KIRA_CONSTEXPR_DISPATCH0(round_); }
    /// Square root of all elements in the vector.
    [[nodiscard]] constexpr auto sqrt() const { return KIRA_CONSTEXPR_DISPATCH0(sqrt_); }
    /// Negate all elements in the vector.
    [[nodiscard]] constexpr auto neg() const { return KIRA_CONSTEXPR_DISPATCH0(neg_); }
    /// Square all elements in the vector.
    [[nodiscard]] constexpr auto sqr() const { return KIRA_CONSTEXPR_DISPATCH0(sqr_); }

    /// \}
    // -----------------------------------------------------------------------------------------------------------------

#undef KIRA_CONSTEXPR_DISPATCH0
#undef KIRA_CONSTEXPR_DISPATCH1
#undef KIRA_CONSTEXPR_DISPATCH2
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
