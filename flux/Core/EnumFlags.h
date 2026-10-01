#pragma once

#include <type_traits>

namespace flux {
/// \brief Marks scoped enumeration \c E as a set of bit flags.
///
/// Specialize it to true to enable \c operator| , \c operator& , and \c any for \c E.
template <typename E> inline constexpr bool isEnumFlags = false;

template <typename E>
    requires(isEnumFlags<E>)
[[nodiscard]] constexpr E operator|(E lhs, E rhs) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

template <typename E>
    requires(isEnumFlags<E>)
[[nodiscard]] constexpr E operator&(E lhs, E rhs) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

/// \brief Returns whether any bit of \p flags is set.
template <typename E>
    requires(isEnumFlags<E>)
[[nodiscard]] constexpr bool any(E flags) noexcept {
    return static_cast<std::underlying_type_t<E>>(flags) != 0;
}
} // namespace flux
