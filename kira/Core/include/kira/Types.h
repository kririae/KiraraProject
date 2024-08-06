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
template <typename T, char... Digits> consteval auto make_integral_udl() {
    constexpr auto self = [](auto self, auto head, auto... tail) consteval {
        if constexpr (sizeof...(tail) == 0)
            return head - '0';
        else
            return 10 * (head - '0') + self(self, tail...);
    };
    return std::integral_constant<T, self(self, Digits...)>{};
}
} // namespace detail

/// UDL for int.
template <char... Digits> consteval auto operator""_i() {
    return detail::make_integral_udl<int, Digits...>();
}

/// UDL for uint.
template <char... Digits> consteval auto operator""_u() {
    return detail::make_integral_udl<uint, Digits...>();
}

static_assert(42_i == 42);
static_assert(std::is_same_v<std::integral_constant<int, 42>, decltype(42_i)>);
static_assert(3_i == 3);
static_assert(std::is_same_v<std::integral_constant<int, 3>, decltype(3_i)>);
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
