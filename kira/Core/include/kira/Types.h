#pragma once

#include <fmt/format.h>

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
template <typename T, char... Cs> consteval auto make_integral_udl() {
    T result = 0;
    ((result = result * 10 + (Cs - '0')), ...);
    return result;
}
} // namespace detail

/// UDL for int.
template <char... Cs> consteval auto operator""_I() {
    constexpr auto value = detail::make_integral_udl<int, Cs...>();
    return std::integral_constant<int, value>{};
}

/// UDL for uint.
template <char... Cs> consteval auto operator""_U() {
    constexpr auto value = detail::make_integral_udl<unsigned int, Cs...>();
    return std::integral_constant<unsigned int, value>{};
}

static_assert(42_I == 42);
static_assert(std::is_same_v<std::integral_constant<int, 42>, decltype(42_I)>);
static_assert(3_I == 3);
static_assert(std::is_same_v<std::integral_constant<int, 3>, decltype(3_I)>);
static_assert(0_I == 0);
static_assert(std::is_same_v<std::integral_constant<int, 0>, decltype(0_I)>);
static_assert(123_I == 123);
static_assert(std::is_same_v<std::integral_constant<int, 123>, decltype(123_I)>);
static_assert(9999_I == 9999);
static_assert(std::is_same_v<std::integral_constant<int, 9999>, decltype(9999_I)>);

static_assert(0_U == 0u);
static_assert(std::is_same_v<std::integral_constant<uint, 0>, decltype(0_U)>);
static_assert(42_U == 42u);
static_assert(std::is_same_v<std::integral_constant<uint, 42>, decltype(42_U)>);
static_assert(9999_U == 9999u);
static_assert(std::is_same_v<std::integral_constant<uint, 9999>, decltype(9999_U)>);

static_assert(10_I + 5_I == 15_I);
static_assert(20_U - 5_U == 15_U);
static_assert(6_I * 7_I == 42_I);
static_assert(100_U / 4_U == 25_U);
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
/// Example: 3.14_R
real constexpr operator"" _R(long double v) { return real(v); }

/// \brief User-defined literal for real (float32) values from integer literals.
///
/// \return A real (float32) representation of the input value.
///
/// Example: 42_R
real constexpr operator"" _R(unsigned long long v) { return real(v); }
/// \}
} // namespace kira

/// A formatter for UDL.
template <typename T, T Value> struct fmt::formatter<std::integral_constant<T, Value>> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(std::integral_constant<T, Value> const &, FormatContext &ctx) const
        -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", Value);
    }
};
