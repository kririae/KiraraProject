#pragma once

#include <cstdint>
#include <type_traits>

namespace kira {
/// \name Basic types
/// \{
/// \brief Single-bit unsigned integer (boolean).
using uint1 = bool;

/// \brief Unsigned character type.
using uchar = unsigned char;

/// \brief 8-bit signed integer.
using int8 = int8_t;
/// \brief 8-bit unsigned integer.
using uint8 = uint8_t;

/// \brief 16-bit signed integer.
using int16 = int16_t;
/// \brief 16-bit unsigned integer.
using uint16 = uint16_t;

/// \brief 32-bit signed integer.
using int32 = int32_t;
/// \brief 32-bit unsigned integer.
using uint32 = uint32_t;
/// \brief Alias for unsigned int.
using uint = unsigned int;

/// \brief 64-bit signed integer.
using int64 = int64_t;
/// \brief 64-bit unsigned integer.
using uint64 = uint64_t;

namespace detail {
template <typename T, char C, char... Cs> consteval auto make_integral_udl() {
    if constexpr (sizeof...(Cs) == 0)
        return C - '0';
    else
        return make_integral_udl<T, C>() * 10 + make_integral_udl<T, Cs...>();
}
} // namespace detail

/// UDL for int.
template <char... Cs> consteval auto operator""_I() {
    constexpr auto value = detail::make_integral_udl<int, Cs...>();
    return std::integral_constant<int, value>{};
}

/// UDL for uint.
template <char... Cs> consteval auto operator""_U() {
    constexpr auto value = detail::make_integral_udl<uint, Cs...>();
    return std::integral_constant<uint, value>{};
}

static_assert(42_I == 42);
static_assert(std::is_same_v<std::integral_constant<int, 42>, decltype(42_I)>);
static_assert(3_I == 3);
static_assert(std::is_same_v<std::integral_constant<int, 3>, decltype(3_I)>);
/// \}

/// \name Floating point types
/// \{
/// \brief 32-bit floating-point type.
using float32 = float;
/// \brief 64-bit floating-point type.
using float64 = double;

/// \brief Alias for float32.
using real = float32;

/// \brief User-defined literal for real (float32) values.
///
/// \return A real (float32) representation of the input value.
///
/// \example 3.14_R
real constexpr operator"" _R(long double v) { return real(v); }

/// \brief User-defined literal for real (float32) values from integer literals.
///
/// \return A real (float32) representation of the input value.
///
/// \example 42_R
real constexpr operator"" _R(unsigned long long v) { return real(v); }
/// \}
} // namespace kira
